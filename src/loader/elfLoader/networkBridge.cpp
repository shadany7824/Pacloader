#if defined(_WIN32) || defined(__MINGW32__)

#define FD_SETSIZE 1024

#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "../log/log.h"
#include "../config/config.h"
#include "../hardware/namco/n2/n2.h"
#include "../hardware/namco/n2/n2Host.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/es1Network.h"
#include "../hardware/namco/es1/wmmt4/es1Wmmt4Network.hpp"
#include "virtualDeviceRegistry.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>
#include <climits>
#include <mutex>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <cstdlib>
#include <new>
#include <unordered_set>
#include <vector>

class HostMutex
{
public:
    HostMutex() { InitializeCriticalSection(&criticalSection); }
    ~HostMutex() { DeleteCriticalSection(&criticalSection); }
    HostMutex(const HostMutex &) = delete;
    HostMutex &operator=(const HostMutex &) = delete;

    void lock() { EnterCriticalSection(&criticalSection); }
    void unlock() { LeaveCriticalSection(&criticalSection); }

private:
    CRITICAL_SECTION criticalSection{};
};

static HostMutex g_net_mutex;
static HostMutex g_socket_mutex;

static constexpr int g_firstSocketDescriptor = 512;
static constexpr int g_lastSocketDescriptor = 1023;
static constexpr int g_socketSlotCount = g_lastSocketDescriptor - g_firstSocketDescriptor + 1;

struct SocketTable
{
    std::atomic<SOCKET> slots[g_socketSlotCount];
    SocketTable()
    {
        for (auto &slot : slots)
            slot.store(INVALID_SOCKET, std::memory_order_relaxed);
    }
};
static SocketTable g_socketTable;
static std::atomic<int> g_es1NetworkTraceCount{0};
static std::atomic<int> g_es1PacketTraceCount{0};
static std::atomic<int> g_es1PacketResultTraceCount{0};
static std::atomic<int> g_es1PacketContentTraceCount{0};

namespace
{
const NamcoN2NetworkConfig *wmmtNetworkConfig()
{
    EmulatorConfig *config = getConfig();
    return config ? &config->namcoN2.network : nullptr;
}

bool parseIPv4(const char *text, in_addr *address)
{
    return text && address && InetPtonA(AF_INET, text, address) == 1;
}

bool getHostIPv4(in_addr *address)
{
    if (!address)
        return false;

    int interfaceIndex = 0;
    int link = 0;
    unsigned char bytes[4] = {};
    unsigned char mask[4] = {};
    unsigned char mac[6] = {};
    const bool haveInterface = es1IsDetected()
        ? es1HostAdapterAddress(bytes) != 0
        : n2HostNetworkInterface(&interfaceIndex, bytes, mask, mac, &link) != 0;
    if (!haveInterface)
        return false;

    std::memcpy(&address->s_addr, bytes, sizeof(bytes));
    return true;
}

bool makeConfiguredBindAddress(const sockaddr *source, int sourceLength,
                               sockaddr_storage &destination, int &destinationLength)
{
    const NamcoN2NetworkConfig *config = wmmtNetworkConfig();
    const bool es1 = es1IsDetected() != 0;
    if ((!config || !config->enabled) && !es1)
        return false;
    if (!source ||
        sourceLength < static_cast<int>(sizeof(sockaddr_in)) || source->sa_family != AF_INET)
        return false;

    in_addr address = {};
    if (es1)
    {
        const sockaddr_in *input = reinterpret_cast<const sockaddr_in *>(source);
        if (input->sin_addr.s_addr == htonl(INADDR_ANY) || !getHostIPv4(&address))
            return false;
    }
    else if (config->bindAddress[0] != '\0')
    {
        if (!parseIPv4(config->bindAddress, &address))
            return false;
    }
    else
    {
        const sockaddr_in *input = reinterpret_cast<const sockaddr_in *>(source);
        if (input->sin_addr.s_addr == htonl(INADDR_ANY))
            return false;
        if (!n2IsWanganTitle() || !getHostIPv4(&address))
            return false;
    }

    std::memset(&destination, 0, sizeof(destination));
    std::memcpy(&destination, source,
                static_cast<size_t>(std::min(sourceLength, static_cast<int>(sizeof(destination)))));
    reinterpret_cast<sockaddr_in *>(&destination)->sin_addr = address;
    destinationLength = sourceLength;
    return true;
}

bool makeEs1GuestAddress(const sockaddr *source, int sourceLength,
                         sockaddr_storage &destination, int &destinationLength)
{
    if (!es1IsDetected() || !source ||
        sourceLength < static_cast<int>(sizeof(sockaddr_in)) || source->sa_family != AF_INET)
        return false;

    unsigned char guestBytes[4] = {};
    if (!es1HostGuestAddress(guestBytes))
        return false;

    in_addr guestAddress = {};
    std::memcpy(&guestAddress.s_addr, guestBytes, sizeof(guestBytes));
    const sockaddr_in *input = reinterpret_cast<const sockaddr_in *>(source);
    if (input->sin_addr.s_addr != guestAddress.s_addr)
        return false;

    std::memset(&destination, 0, sizeof(destination));
    std::memcpy(&destination, source,
                static_cast<size_t>(std::min(sourceLength, static_cast<int>(sizeof(destination)))));
    // The guest socket is bound to the host adapter, so send there and let the
    // guest still see its ES1 address in the received sockaddr.
    in_addr hostAddress = {};
    if (!getHostIPv4(&hostAddress))
        return false;
    reinterpret_cast<sockaddr_in *>(&destination)->sin_addr = hostAddress;
    destinationLength = sourceLength;
    log_debug("System ES1: mapped guest destination %u.%u.%u.%u to host adapter",
              guestBytes[0], guestBytes[1], guestBytes[2], guestBytes[3]);
    return true;
}

bool isEs1MessagePacket(const uint8_t *data, size_t length)
{
    /* The datagram is the complete clLanBuffer, not just its payload. */
    if (!data || length < 0x18)
        return false;
    const uint16_t declaredLength = static_cast<uint16_t>(data[0]) |
                                    (static_cast<uint16_t>(data[1]) << 8);
    const uint32_t type = *reinterpret_cast<const uint32_t *>(data + 0x14);
    return declaredLength >= 0x18 && declaredLength <= length &&
           type >= 0x902 && type <= 0x90a;
}

void rememberEs1OutgoingPcb(const LinuxMsghdr *message)
{
    if (!es1IsDetected() || !message || !message->name || message->nameLength < sizeof(sockaddr_in) ||
        !message->iov || message->iovCount == 0 ||
        message->iov[0].length < 36)
        return;

    const sockaddr_in *name = static_cast<const sockaddr_in *>(message->name);
    const uint8_t *data = static_cast<const uint8_t *>(message->iov[0].base);
    if (g_es1PacketContentTraceCount.fetch_add(1) < 32)
        log_debug("ES1 raw tx port=%u len=%u data=%02x %02x %02x %02x src=%08x type=%08x", ntohs(name->sin_port),
                 static_cast<unsigned>(message->iov[0].length), data[0], data[1], data[2], data[3],
                 *reinterpret_cast<const uint32_t *>(data + 8),
                 *reinterpret_cast<const uint32_t *>(data + 0x14));
    if (ntohs(name->sin_port) == 50765 && isEs1MessagePacket(data, message->iov[0].length))
    {
    log_debug("ES1 clLanBuffer tx len=%u src=%08x type=%08x", data[0] | (data[1] << 8),
                 *reinterpret_cast<const uint32_t *>(data + 8),
                 *reinterpret_cast<const uint32_t *>(data + 0x14));
    }
}

void rewriteEs1MessagePeerAddress(sockaddr *address, int addressLength,
                                  const uint8_t *data, size_t length)
{
    if (!es1IsDetected() || !address || addressLength < static_cast<int>(sizeof(sockaddr_in)) ||
        address->sa_family != AF_INET || !isEs1MessagePacket(data, length))
        return;

    unsigned char peerBytes[4] = {};
    if (!es1HostGuestAddress(peerBytes))
        return;

    const uint32_t type = *reinterpret_cast<const uint32_t *>(data + 0x14);
    if (type == 0x903 && length >= 0x1c)
    {
        /* clNet::receive takes the low octet of the peer address as the PCB
         * slot, so passing the host adapter address through unchanged makes the
         * game index a nonexistent slot and report a false duplicate. */
        const unsigned char pcb = data[0x1b];
        if (pcb >= 1 && pcb <= 4)
            peerBytes[3] = pcb;
    }
    std::memcpy(&reinterpret_cast<sockaddr_in *>(address)->sin_addr.s_addr,
                peerBytes, sizeof(peerBytes));
}

bool isEs1MulticastAddress(const sockaddr *source, int sourceLength)
{
    if (!es1IsDetected() || !source ||
        sourceLength < static_cast<int>(sizeof(sockaddr_in)) || source->sa_family != AF_INET)
        return false;

    const sockaddr_in *input = reinterpret_cast<const sockaddr_in *>(source);
    return ntohl(input->sin_addr.s_addr) == 0xe1000001u;
}

bool makeEs1MulticastAddress(const sockaddr *source, int sourceLength,
                             sockaddr_storage &destination, int &destinationLength)
{
    if (!isEs1MulticastAddress(source, sourceLength))
        return false;

    std::memset(&destination, 0, sizeof(destination));
    std::memcpy(&destination, source,
                static_cast<size_t>(std::min(sourceLength, static_cast<int>(sizeof(destination)))));
    // A lone virtual cabinet has to hear its own discovery datagrams, so the
    // local endpoint is the host adapter rather than the multicast address.
    in_addr hostAddress = {};
    if (!getHostIPv4(&hostAddress))
        return false;
    reinterpret_cast<sockaddr_in *>(&destination)->sin_addr = hostAddress;
    destinationLength = sourceLength;
    log_debug("System ES1: mapped discovery multicast 225.0.0.1 to the host adapter");
    return true;
}

bool makeConfiguredBroadcastAddress(const sockaddr *source, int sourceLength,
                                    sockaddr_storage &destination, int &destinationLength)
{
    const NamcoN2NetworkConfig *config = wmmtNetworkConfig();
    if (!config || !config->enabled || !config->rewriteBroadcast ||
        config->broadcastAddress[0] == '\0' || !source ||
        sourceLength < static_cast<int>(sizeof(sockaddr_in)) || source->sa_family != AF_INET)
        return false;

    const sockaddr_in *input = reinterpret_cast<const sockaddr_in *>(source);
    const uint32_t hostAddress = ntohl(input->sin_addr.s_addr);
    if (hostAddress != INADDR_BROADCAST && (hostAddress & 0xFFu) != 0xFFu)
        return false;

    in_addr address = {};
    if (!parseIPv4(config->broadcastAddress, &address))
        return false;

    std::memset(&destination, 0, sizeof(destination));
    std::memcpy(&destination, source,
                static_cast<size_t>(std::min(sourceLength, static_cast<int>(sizeof(destination)))));
    reinterpret_cast<sockaddr_in *>(&destination)->sin_addr = address;
    destinationLength = sourceLength;
    return true;
}
} // namespace

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

/*
 * WSA error to guest errno.  These are Linux i386's numbers, written out
 * literally because <cerrno> here would give the host CRT's - and the two
 * disagree on the socket codes that matter (EINPROGRESS is 115, not 112).
 */
namespace LinuxErrno
{
constexpr int Intr = 4;
constexpr int Io = 5;
constexpr int Badf = 9;
constexpr int Again = 11;
constexpr int Acces = 13;
constexpr int Fault = 14;
constexpr int Inval = 22;
constexpr int Mfile = 24;
constexpr int NameTooLong = 36;
constexpr int NotEmpty = 39;
constexpr int Loop = 40;
constexpr int Users = 87;
constexpr int NotSock = 88;
constexpr int DestAddrReq = 89;
constexpr int MsgSize = 90;
constexpr int ProtoType = 91;
constexpr int NoProtoOpt = 92;
constexpr int ProtoNoSupport = 93;
constexpr int SockTNoSupport = 94;
constexpr int OpNotSupp = 95;
constexpr int PfNoSupport = 96;
constexpr int AfNoSupport = 97;
constexpr int AddrInUse = 98;
constexpr int AddrNotAvail = 99;
constexpr int NetDown = 100;
constexpr int NetUnreach = 101;
constexpr int NetReset = 102;
constexpr int ConnAborted = 103;
constexpr int ConnReset = 104;
constexpr int NoBufs = 105;
constexpr int IsConn = 106;
constexpr int NotConn = 107;
constexpr int Shutdown = 108;
constexpr int TooManyRefs = 109;
constexpr int TimedOut = 110;
constexpr int ConnRefused = 111;
constexpr int HostDown = 112;
constexpr int HostUnreach = 113;
constexpr int Already = 114;
constexpr int InProgress = 115;
constexpr int Stale = 116;
constexpr int Remote = 66;
constexpr int DQuot = 122;
} // namespace LinuxErrno

static int mapWSAErrorToErrno(int wsaError)
{
    switch (wsaError)
    {
        case WSAEINTR:
            return LinuxErrno::Intr;
        case WSAEBADF:
            return LinuxErrno::Badf;
        case WSAEACCES:
            return LinuxErrno::Acces;
        case WSAEFAULT:
            return LinuxErrno::Fault;
        case WSAEINVAL:
            return LinuxErrno::Inval;
        case WSAEMFILE:
            return LinuxErrno::Mfile;
        case WSAEWOULDBLOCK:
            return LinuxErrno::Again;
        case WSAEINPROGRESS:
        case WSA_IO_PENDING:
            return LinuxErrno::InProgress;
        case WSAEALREADY:
            return LinuxErrno::Already;
        case WSAENOTSOCK:
            return LinuxErrno::NotSock;
        case WSAEDESTADDRREQ:
            return LinuxErrno::DestAddrReq;
        case WSAEMSGSIZE:
            return LinuxErrno::MsgSize;
        case WSAEPROTOTYPE:
            return LinuxErrno::ProtoType;
        case WSAENOPROTOOPT:
            return LinuxErrno::NoProtoOpt;
        case WSAEPROTONOSUPPORT:
            return LinuxErrno::ProtoNoSupport;
        case WSAESOCKTNOSUPPORT:
            return LinuxErrno::SockTNoSupport;
        case WSAEOPNOTSUPP:
            return LinuxErrno::OpNotSupp;
        case WSAEPFNOSUPPORT:
            return LinuxErrno::PfNoSupport;
        case WSAEAFNOSUPPORT:
            return LinuxErrno::AfNoSupport;
        case WSAEADDRINUSE:
            return LinuxErrno::AddrInUse;
        case WSAEADDRNOTAVAIL:
            return LinuxErrno::AddrNotAvail;
        case WSAENETDOWN:
            return LinuxErrno::NetDown;
        case WSAENETUNREACH:
            return LinuxErrno::NetUnreach;
        case WSAENETRESET:
            return LinuxErrno::NetReset;
        case WSAECONNABORTED:
            return LinuxErrno::ConnAborted;
        case WSAECONNRESET:
            return LinuxErrno::ConnReset;
        case WSAENOBUFS:
            return LinuxErrno::NoBufs;
        case WSAEISCONN:
            return LinuxErrno::IsConn;
        case WSAENOTCONN:
            return LinuxErrno::NotConn;
        case WSAESHUTDOWN:
            return LinuxErrno::Shutdown;
        case WSAETOOMANYREFS:
            return LinuxErrno::TooManyRefs;
        case WSAETIMEDOUT:
            return LinuxErrno::TimedOut;
        case WSAECONNREFUSED:
            return LinuxErrno::ConnRefused;
        case WSAELOOP:
            return LinuxErrno::Loop;
        case WSAENAMETOOLONG:
            return LinuxErrno::NameTooLong;
        case WSAEHOSTDOWN:
            return LinuxErrno::HostDown;
        case WSAEHOSTUNREACH:
            return LinuxErrno::HostUnreach;
        case WSAENOTEMPTY:
            return LinuxErrno::NotEmpty;
        case WSAEPROCLIM:
            return LinuxErrno::Users;
        case WSAEUSERS:
            return LinuxErrno::Users;
        case WSAEDQUOT:
            return LinuxErrno::DQuot;
        case WSAESTALE:
            return LinuxErrno::Stale;
        case WSAEREMOTE:
            return LinuxErrno::Remote;
        default:
            return LinuxErrno::Inval;
    }
}

namespace NetworkBridge
{
    static int registerSocket(SOCKET socket)
    {
        std::lock_guard<HostMutex> lock(g_socket_mutex);

        int freeSlot = -1;
        for (int slot = 0; slot < g_socketSlotCount; slot++)
        {
            const SOCKET held = g_socketTable.slots[slot].load(std::memory_order_relaxed);
            if (held == socket)
                return g_firstSocketDescriptor + slot;
            if (held == INVALID_SOCKET && freeSlot < 0)
                freeSlot = slot;
        }

        if (freeSlot < 0)
        {
            log_error("Network bridge: no descriptor left for a new socket (%d in use)", g_socketSlotCount);
            return -1;
        }

        g_socketTable.slots[freeSlot].store(socket, std::memory_order_release);
        return g_firstSocketDescriptor + freeSlot;
    }

    SOCKET hostSocket(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return static_cast<SOCKET>(descriptor);
        const SOCKET held = g_socketTable.slots[descriptor - g_firstSocketDescriptor].load(std::memory_order_acquire);
        return held == INVALID_SOCKET ? static_cast<SOCKET>(descriptor) : held;
    }

    // The reverse of hostSocket(), for handing select() results back.
    int guestDescriptor(SOCKET socket)
    {
        for (int slot = 0; slot < g_socketSlotCount; slot++)
        {
            if (g_socketTable.slots[slot].load(std::memory_order_relaxed) == socket)
                return g_firstSocketDescriptor + slot;
        }
        return static_cast<int>(socket);
    }

    void forgetSocket(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return;
        std::lock_guard<HostMutex> lock(g_socket_mutex);
        g_socketTable.slots[descriptor - g_firstSocketDescriptor].store(INVALID_SOCKET, std::memory_order_release);
    }

    void initBridges()
    {
        log_info("Initializing Network Bridges...");

        MAP("socket", bridgeSocket);
        MAP("connect", bridgeConnect);
        MAP("bind", bridgeBind);
        MAP("listen", bridgeListen);
        MAP("accept", bridgeAccept);
        MAP("send", bridgeSend);
        MAP("recv", bridgeRecv);
        MAP("sendto", bridgeSendto);
        MAP("recvfrom", bridgeRecvfrom);
        MAP("sendmsg", bridgeSendmsg);
        MAP("recvmsg", bridgeRecvmsg);
        MAP("getpeername", bridgeGetpeername);
        MAP("getsockname", bridgeGetsockname);
        MAP("shutdown", bridgeShutdown);
        MAP("setsockopt", bridgeSetsockopt);
        MAP("getsockopt", bridgeGetsockopt);
        MAP("inet_pton", bridgeInet_pton);
        MAP("inet_ntop", bridgeInet_ntop);
        MAP("inet_aton", bridgeInet_aton);
        MAP("inet_addr", bridgeInet_addr);
        MAP("inet_ntoa", bridgeInet_ntoa);
        MAP("ntohs", bridgeNtohs);
        MAP("htons", bridgeHtons);
        MAP("htonl", bridgeHtonl);
        MAP("ntohl", bridgeNtohl);
        MAP("gethostbyname_r", bridgeGethostbyname_r);
        MAP("gethostbyaddr_r", bridgeGethostbyaddr_r);
        MAP("gethostbyname", bridgeGethostbyname);
        MAP("gethostbyaddr", bridgeGethostbyaddr);
        MAP("gethostname", bridgeGethostname);
        MAP("getservbyname", bridgeGetservbyname);
        MAP("getaddrinfo", bridgeGetaddrinfo);
        MAP("freeaddrinfo", bridgeFreeaddrinfo);
        MAP("if_nametoindex", bridgeIf_nametoindex);
        MAP("if_indextoname", bridgeIf_indextoname);
        MAP("getifaddrs", bridgeGetifaddrs);
        MAP("freeifaddrs", bridgeFreeifaddrs);
    }

    /* Linux struct ifaddrs as the i386 guest sees it.  Titles enumerate
     * interfaces to find their own address, so the list is the cabinet's:
     * loopback plus a single eth0. */
    struct LinuxIfaddrs
    {
        LinuxIfaddrs *ifa_next;
        char *ifa_name;
        unsigned int ifa_flags;
        sockaddr *ifa_addr;
        sockaddr *ifa_netmask;
        sockaddr *ifa_broadaddr;
        void *ifa_data;
    };

    /* Linux IFF_* values, which do not match the Windows ones. */
    constexpr unsigned int LINUX_IFF_UP = 0x1;
    constexpr unsigned int LINUX_IFF_BROADCAST = 0x2;
    constexpr unsigned int LINUX_IFF_LOOPBACK = 0x8;
    constexpr unsigned int LINUX_IFF_RUNNING = 0x40;
    constexpr unsigned int LINUX_IFF_MULTICAST = 0x1000;

    /* The address the guest's single "eth0" stands for.  Both getifaddrs and
     * the SIOCGIF* ioctls answer from here so they never disagree. */
    void primaryHostInterface(unsigned long &address, unsigned long &netmask,
                              unsigned char mac[6])
    {
        address = 0;
        netmask = htonl(0xFFFFFF00u);
        std::memset(mac, 0, 6);

        ULONG adapterInfoSize = 0;
        if (GetAdaptersInfo(nullptr, &adapterInfoSize) != ERROR_BUFFER_OVERFLOW || !adapterInfoSize)
            return;

        std::vector<unsigned char> buffer(adapterInfoSize);
        auto *adapters = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());
        if (GetAdaptersInfo(adapters, &adapterInfoSize) != NO_ERROR)
            return;

        for (IP_ADAPTER_INFO *adapter = adapters; adapter; adapter = adapter->Next)
        {
            const unsigned long candidate = inet_addr(adapter->IpAddressList.IpAddress.String);
            if (candidate == 0 || candidate == INADDR_NONE)
                continue;

            address = candidate;
            const unsigned long mask = inet_addr(adapter->IpAddressList.IpMask.String);
            if (mask != 0 && mask != INADDR_NONE)
                netmask = mask;
            std::memcpy(mac, adapter->Address, std::min<UINT>(adapter->AddressLength, 6));
            return;
        }
    }

    /* One allocation per interface keeps freeifaddrs simple: the guest only
     * ever hands back the head of the list. */
    struct IfaddrsEntry
    {
        LinuxIfaddrs node;
        char name[16];
        sockaddr_in address;
        sockaddr_in netmask;
        sockaddr_in broadcast;
    };

    static IfaddrsEntry *makeIfaddrsEntry(const char *name, unsigned long address,
                                          unsigned long netmask, unsigned int flags)
    {
        IfaddrsEntry *entry = new (std::nothrow) IfaddrsEntry{};
        if (!entry)
            return nullptr;

        std::strncpy(entry->name, name, sizeof(entry->name) - 1);

        entry->address.sin_family = AF_INET;
        entry->address.sin_addr.s_addr = address;
        entry->netmask.sin_family = AF_INET;
        entry->netmask.sin_addr.s_addr = netmask;
        entry->broadcast.sin_family = AF_INET;
        entry->broadcast.sin_addr.s_addr = (address & netmask) | ~netmask;

        entry->node.ifa_name = entry->name;
        entry->node.ifa_flags = flags;
        entry->node.ifa_addr = reinterpret_cast<sockaddr *>(&entry->address);
        entry->node.ifa_netmask = reinterpret_cast<sockaddr *>(&entry->netmask);
        entry->node.ifa_broadaddr = reinterpret_cast<sockaddr *>(&entry->broadcast);
        return entry;
    }

    int bridgeGetifaddrs(void **result)
    {
        if (!result)
            return -1;
        *result = nullptr;

        std::vector<IfaddrsEntry *> entries;

        if (IfaddrsEntry *loopback = makeIfaddrsEntry(
                "lo", htonl(INADDR_LOOPBACK), htonl(0xFF000000u),
                LINUX_IFF_UP | LINUX_IFF_LOOPBACK | LINUX_IFF_RUNNING))
            entries.push_back(loopback);

        /* Present the host adapter the ES1 layer already picked for the
         * virtual eth0, so every path agrees on the cabinet's address. */
        unsigned long address = 0;
        unsigned long netmask = 0;
        unsigned char mac[6]{};
        primaryHostInterface(address, netmask, mac);

        if (address != 0)
        {
            if (IfaddrsEntry *ethernet = makeIfaddrsEntry(
                    "eth0", address, netmask,
                    LINUX_IFF_UP | LINUX_IFF_BROADCAST | LINUX_IFF_RUNNING | LINUX_IFF_MULTICAST))
                entries.push_back(ethernet);
        }

        for (size_t i = 0; i + 1 < entries.size(); ++i)
            entries[i]->node.ifa_next = &entries[i + 1]->node;

        if (!entries.empty())
            *result = &entries.front()->node;

        log_debug("getifaddrs -> %zu interface(s)", entries.size());
        return 0;
    }

    void bridgeFreeifaddrs(void *list)
    {
        auto *node = static_cast<LinuxIfaddrs *>(list);
        while (node)
        {
            LinuxIfaddrs *next = node->ifa_next;
            /* Every node is the first member of its IfaddrsEntry. */
            delete reinterpret_cast<IfaddrsEntry *>(node);
            node = next;
        }
    }

    unsigned long bridgeInet_addr(const char *cp)
    {
        // cp = "127.0.0.1";
        return inet_addr(cp);
    }

    int bridgeInet_aton(const char *cp, struct in_addr *inp)
    {
        unsigned long addr = bridgeInet_addr(cp);
        if (addr == INADDR_NONE && strcmp(cp, "255.255.255.255") != 0)
            return 0;
        if (inp)
            inp->s_addr = addr;
        return 1;
    }

    int bridgeInet_pton(int af, const char *src, void *dst)
    {
        return InetPtonA(af, src, dst);
    }

    const char *bridgeInet_ntop(int af, const void *src, char *dst, size_t size)
    {
        if (!src || !dst || size == 0)
        {
            errno = EINVAL;
            return nullptr;
        }

        const char *result = InetNtopA(af, const_cast<void *>(src), dst, size);
        if (!result)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return result;
    }

    char *bridgeInet_ntoa(struct in_addr in)
    {
        return inet_ntoa(in);
    }
    uint16_t bridgeNtohs(uint16_t netshort)
    {
        return ntohs(netshort);
    }
    uint16_t bridgeHtons(uint16_t hostshort)
    {
        return htons(hostshort);
    }
    uint32_t bridgeNtohl(uint32_t netlong)
    {
        return ntohl(netlong);
    }
    uint32_t bridgeHtonl(uint32_t hostlong)
    {
        return htonl(hostlong);
    }

    bool isSocketDescriptor(int descriptor)
    {
        if (descriptor < g_firstSocketDescriptor || descriptor > g_lastSocketDescriptor)
            return false;
        return g_socketTable.slots[descriptor - g_firstSocketDescriptor].load(std::memory_order_acquire) != INVALID_SOCKET;
    }

    int bridgeSocketRead(int descriptor, void *buffer, size_t length)
    {
        return bridgeRecv(static_cast<SOCKET>(descriptor), static_cast<char *>(buffer),
                          static_cast<int>(std::min(length, static_cast<size_t>(INT_MAX))), 0);
    }

    int bridgeSocketWrite(int descriptor, const void *buffer, size_t length)
    {
        return bridgeSend(static_cast<SOCKET>(descriptor), static_cast<const char *>(buffer),
                          static_cast<int>(std::min(length, static_cast<size_t>(INT_MAX))), 0);
    }

    int bridgeSocketClose(int descriptor)
    {
        const SOCKET socket = NetworkBridge::hostSocket(descriptor);
        NetworkBridge::forgetSocket(descriptor);
        const int result = closesocket(socket);
        if (result == SOCKET_ERROR)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return result;
    }
}; // namespace NetworkBridge

extern "C" SOCKET bridgeSocket(int af, int type, int protocol)
{
    log_trace(">>> socket called: af=%d, type=%d, protocol=%d", af, type, protocol);
    SOCKET s = socket(af, type, protocol);
    if (s == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }

    const NamcoN2NetworkConfig *config = wmmtNetworkConfig();
    const bool es1Network = es1IsDetected();
    if ((es1Network || (config && config->enabled && config->allowBroadcast)) &&
        type == SOCK_DGRAM)
    {
        const int enabled = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char *>(&enabled), sizeof(enabled));
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&enabled), sizeof(enabled));
    }

    const int descriptor = NetworkBridge::registerSocket(s);
    if (descriptor < 0)
    {
        closesocket(s);
        errno = EMFILE;
        return (SOCKET)-1;
    }
    if (g_es1NetworkTraceCount.fetch_add(1) < 8)
        log_debug("Network bridge: socket guest=%d host=%lld type=%d", descriptor,
                 (long long)s, type);
    log_trace(">>> socket EXIT: handle %lld as descriptor %d", (long long)s, descriptor);
    return static_cast<SOCKET>(descriptor);
}

namespace Es1CompatBridge
{
void forgetEdgeState(int fd);
}

extern "C" int bridgeConnect(SOCKET s, const struct sockaddr *name, int namelen)
{
    const int guestSocket = static_cast<int>(s);
    /* connect() changes the socket's readiness; the epoll shim's edge memory
     * for it is stale from here on. */
    Es1CompatBridge::forgetEdgeState(guestSocket);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> connect called: socket=%lld", (long long)s);
    sockaddr_storage configuredAddress = {};
    int configuredLength = namelen;
    bool rewritten = makeEs1MulticastAddress(name, namelen, configuredAddress, configuredLength);
    if (!rewritten)
        rewritten = makeEs1GuestAddress(name, namelen, configuredAddress, configuredLength);
    if (!rewritten && es1IsDetected() && name &&
        namelen >= static_cast<int>(sizeof(sockaddr_in)) &&
        name->sa_family == AF_INET &&
        ntohs(reinterpret_cast<const sockaddr_in *>(name)->sin_port) == 50765)
    {
        /* clNet may pass the already-normalized virtual cabinet address to
         * connect(), while the discovery datagrams still use 225.0.0.1.
         * Both refer to the in-process ES1 terminal in a one-cabinet setup. */
        rewritten = makeConfiguredBindAddress(name, namelen,
                                              configuredAddress, configuredLength);
    }
    const sockaddr *destination = rewritten ? reinterpret_cast<const sockaddr *>(&configuredAddress) : name;
    const bool es1LocalTerminal = es1IsDetected() && rewritten && destination &&
                                  destination->sa_family == AF_INET &&
                                  ntohs(reinterpret_cast<const sockaddr_in *>(destination)->sin_port) == 50765;
    if (g_es1NetworkTraceCount.fetch_add(1) < 16)
        log_debug("Network bridge: connect rewritten=%d port=%u", rewritten ? 1 : 0,
                 ntohs(reinterpret_cast<const sockaddr_in *>(destination)->sin_port));
    // For the in-process ES1 terminal, finish the loopback handshake here so
    // the two sessions are a real pair before the guest's protocol starts.
    u_long restoreNonBlocking = 0;
    const bool restoreSocketMode = es1LocalTerminal &&
                                   ioctlsocket(s, FIONBIO, &restoreNonBlocking) == 0;
    if (restoreSocketMode)
    {
        u_long blocking = 0;
        ioctlsocket(s, FIONBIO, &blocking);
    }
    int ret = connect(s, destination, configuredLength);
    if (restoreSocketMode)
    {
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
    }
    if (ret == SOCKET_ERROR)
    {
        const int wsaError = WSAGetLastError();
        // Winsock can answer WSAEWOULDBLOCK even after the loopback listener
        // has completed the handshake, so settle it here.  Confined to that
        // in-process peer: doing it for a remote server turns every async
        // connect into a blocking wait and reports the wrong errno afterwards.
        if (es1LocalTerminal && wsaError == WSAEWOULDBLOCK)
        {
            // The peer is the in-process loopback listener.  Complete the
            // short local handshake synchronously, then report success to
            // the guest's asynchronous session machinery.
            fd_set writeSet;
            fd_set exceptionSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptionSet);
            FD_SET(s, &writeSet);
            FD_SET(s, &exceptionSet);
            timeval timeout = {1, 0};
            if (select(0, nullptr, &writeSet, &exceptionSet, &timeout) > 0)
            {
                int socketError = 0;
                int socketErrorLength = sizeof(socketError);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char *>(&socketError), &socketErrorLength) == 0 &&
                    socketError == 0)
                {
                    ret = 0;
                    errno = 0;
                }
            }
        }
        if (ret == SOCKET_ERROR)
        {
            /* Report what connect() itself produced, not the last-error state
             * of the select/getsockopt in between.  connect() is the one call
             * where WSAEWOULDBLOCK means EINPROGRESS rather than EAGAIN, and
             * callers test for exactly that. */
            errno = wsaError == WSAEWOULDBLOCK ? LinuxErrno::InProgress
                                               : mapWSAErrorToErrno(wsaError);
            log_debug("Network bridge: connect failed WSAError=%d errno=%d", wsaError, errno);
            WSASetLastError(wsaError);
        }
    }
    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait && destination && destination->sa_family == AF_INET)
    {
        const sockaddr_in *target = reinterpret_cast<const sockaddr_in *>(destination);
        char text[INET_ADDRSTRLEN] = {};
        InetNtopA(AF_INET, &target->sin_addr, text, sizeof(text));
        log_info("LLCONNECT fd=%d -> %s:%u result=%d errno=%d", guestSocket, text,
                 ntohs(target->sin_port), ret, ret == SOCKET_ERROR ? errno : 0);
    }
    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 connect fd=%d result=%d errno=%d wsa=%d", guestSocket, ret,
                 ret == SOCKET_ERROR ? errno : 0, ret == SOCKET_ERROR ? WSAGetLastError() : 0);
    log_trace(">>> connect EXIT: returning %d (WSAError=%d)", ret, ret < 0 ? WSAGetLastError() : 0);
    return ret;
}

extern "C" int bridgeBind(SOCKET s, const struct sockaddr *name, int namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> bind called: socket=%lld", (long long)s);
    sockaddr_storage configuredAddress = {};
    int configuredLength = namelen;
    const bool rewritten = makeConfiguredBindAddress(name, namelen, configuredAddress, configuredLength);
    const sockaddr *bindAddress = rewritten ? reinterpret_cast<const sockaddr *>(&configuredAddress) : name;
    if (g_es1NetworkTraceCount.fetch_add(1) < 24)
        log_debug("Network bridge: bind rewritten=%d port=%u", rewritten ? 1 : 0,
                 ntohs(reinterpret_cast<const sockaddr_in *>(bindAddress)->sin_port));
    int ret = bind(s, bindAddress, configuredLength);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_debug("Network bridge: bind failed WSAError=%d errno=%d rewritten=%d",
                  WSAGetLastError(), errno, rewritten ? 1 : 0);
    }
    else if (rewritten)
        log_debug("Network bridge: bind address mapped to the selected host adapter");
    log_trace(">>> bind EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeListen(SOCKET s, int backlog)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> listen called: socket=%lld, backlog=%d", (long long)s, backlog);
    int ret = listen(s, backlog);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 listen fd=%d result=%d errno=%d", guestSocket, ret,
                 ret == SOCKET_ERROR ? errno : 0);
    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 recvfrom result=%d errno=%d", ret, ret == SOCKET_ERROR ? errno : 0);
    log_trace(">>> listen EXIT: returning %d", ret);
    return ret;
}

extern "C" SOCKET bridgeAccept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> accept ENTRY: socket=%lld (THIS MAY BLOCK!)", (long long)s);
    SOCKET ret = accept(s, addr, addrlen);
    if (ret == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return (SOCKET)-1;
    }
    /* Restore the cabinet-visible peer on the accepted socket, as recvmsg does
     * for UDP; otherwise clLanSession rejects its own terminal. */
    if (es1IsDetected() && addr && addrlen &&
        *addrlen >= static_cast<int>(sizeof(sockaddr_in)) &&
        addr->sa_family == AF_INET)
    {
        unsigned char adapterBytes[4] = {};
        unsigned char guestBytes[4] = {};
        if (es1HostAdapterAddress(adapterBytes) && es1HostGuestAddress(guestBytes))
        {
            in_addr adapterAddress = {};
            in_addr guestAddress = {};
            std::memcpy(&adapterAddress.s_addr, adapterBytes, sizeof(adapterBytes));
            std::memcpy(&guestAddress.s_addr, guestBytes, sizeof(guestBytes));
            auto *peer = reinterpret_cast<sockaddr_in *>(addr);
            if (peer->sin_addr.s_addr == adapterAddress.s_addr)
                peer->sin_addr = guestAddress;
        }
    }
    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 accept fd=%d host=%lld", guestSocket, (long long)ret);
    const int descriptor = NetworkBridge::registerSocket(ret);
    if (descriptor < 0)
    {
        closesocket(ret);
        errno = EMFILE;
        return (SOCKET)-1;
    }
    log_trace(">>> accept EXIT: handle %lld as descriptor %d", (long long)ret, descriptor);
    return static_cast<SOCKET>(descriptor);
}

extern "C" int bridgeShutdown(SOCKET s, int how)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    const int ret = shutdown(s, how);
    if (ret == SOCKET_ERROR)
        errno = mapWSAErrorToErrno(WSAGetLastError());
    return ret;
}

extern "C" int bridgeRecv(SOCKET s, char *buf, int len, int flags)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 recv fd=%d len=%d flags=0x%x", guestSocket, len, flags);
    log_trace(">>> recv ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);

    if (flags & 0x40)
    {
        flags &= ~0x40;
    }

    int ret = recv(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 sendto result=%d errno=%d", ret, ret == SOCKET_ERROR ? errno : 0);
    log_trace(">>> recv EXIT: returning %d", ret);
    return ret;
}

extern "C" int bridgeSend(SOCKET s, const char *buf, int len, int flags)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 send fd=%d len=%d flags=0x%x", guestSocket, len, flags);
    log_trace(">>> send called: socket=%lld, len=%d", (long long)s, len);

    flags &= ~(0x4000 | 0x40);

    int ret = send(s, buf, len, flags);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait)
        log_info("LLSEND fd=%d len=%d -> %d errno=%d", guestSocket, len, ret,
                 ret == SOCKET_ERROR ? errno : 0);
    log_trace(">>> send EXIT: returning %d", ret);
    return ret;
}

static void traceEs1Datagram(const char *direction, const struct sockaddr *peer, int peerLength,
                             const char *payload, int length);

extern "C" int bridgeRecvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 recvfrom fd=%d len=%d flags=0x%x", guestSocket, len, flags);
    log_trace(">>> recvfrom ENTRY: socket=%lld, len=%d (THIS MAY BLOCK!)", (long long)s, len);
    if (flags & 0x40)
        flags &= ~0x40;
    int ret = recvfrom(s, buf, len, flags, from, fromlen);
    if (ret > 0)
        traceEs1Datagram("recvfrom", from, fromlen ? *fromlen : 0, buf, ret);
    if (ret >= 0)
    {
        rewriteEs1MessagePeerAddress(from, fromlen ? *fromlen : 0,
                                     reinterpret_cast<const uint8_t *>(buf),
                                     static_cast<size_t>(ret));
    }
    if (ret >= 0 && g_es1NetworkTraceCount.fetch_add(1) < 32)
        log_debug("Network bridge: recvfrom bytes=%d", ret);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    /* The cabinet keeps its own LAN session alive by receiving the beacons it
     * broadcasts; a gap here is what produces "local: session timeout". */
    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait && ret > 0)
    {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(from);
        log_info("LLRECVFROM fd=%d bytes=%d src=%s:%u", guestSocket, ret,
                 from && from->sa_family == AF_INET ? inet_ntoa(ipv4->sin_addr) : "?",
                 from && from->sa_family == AF_INET ? ntohs(ipv4->sin_port) : 0u);
    }
    log_trace(">>> recvfrom EXIT: returning %d", ret);
    return ret;
}

/* Dumps the ES1 cabinet-to-terminal beacons (UDP 9388), bounded because they
 * repeat about twice a second.  Enabled by LL_ES1_PACKET_TRACE. */
static void traceEs1Datagram(const char *direction, const struct sockaddr *peer, int peerLength,
                             const char *payload, int length)
{
    static const bool enabled = std::getenv("LL_ES1_PACKET_TRACE") != nullptr;
    if (!enabled || length <= 0 || !payload)
        return;

    static std::atomic<int> budget{0};
    if (budget.fetch_add(1) >= 64)
        return;

    char address[64] = "?";
    if (peer && peerLength >= static_cast<int>(sizeof(sockaddr_in)) && peer->sa_family == AF_INET)
    {
        const auto *in = reinterpret_cast<const sockaddr_in *>(peer);
        const unsigned char *octets = reinterpret_cast<const unsigned char *>(&in->sin_addr);
        std::snprintf(address, sizeof(address), "%u.%u.%u.%u:%u", octets[0], octets[1], octets[2],
                      octets[3], ntohs(in->sin_port));
    }

    const int shown = std::min(length, 64);
    char hex[64 * 3 + 1] = {0};
    for (int i = 0; i < shown; ++i)
        std::snprintf(hex + i * 3, 4, "%02x ", static_cast<unsigned char>(payload[i]));

    log_debug("ES1 packet %s %s len=%d: %s", direction, address, length, hex);
}

extern "C" int bridgeSendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 sendto fd=%d len=%d flags=0x%x", guestSocket, len, flags);
    log_trace(">>> sendto called: socket=%lld, len=%d", (long long)s, len);
    flags &= ~(0x4000 | 0x40);
    sockaddr_storage configuredAddress = {};
    int configuredLength = tolen;
    /* Leave the discovery destination as multicast: the guest's
     * IP_MULTICAST_IF already selects the adapter, and rewriting it to unicast
     * makes the cabinet hear its own status twice. */
    bool rewritten = !isEs1MulticastAddress(to, tolen) &&
                     makeEs1MulticastAddress(to, tolen, configuredAddress, configuredLength);
    if (!rewritten)
        rewritten = makeConfiguredBroadcastAddress(to, tolen, configuredAddress, configuredLength);
    const sockaddr *destination = rewritten ? reinterpret_cast<const sockaddr *>(&configuredAddress) : to;
    if (!rewritten)
        rewritten = makeEs1GuestAddress(to, tolen, configuredAddress, configuredLength);
    destination = rewritten ? reinterpret_cast<const sockaddr *>(&configuredAddress) : to;
    if (g_es1NetworkTraceCount.fetch_add(1) < 32)
        log_debug("Network bridge: sendto bytes=%d rewritten=%d port=%u", len,
                 rewritten ? 1 : 0,
                 ntohs(reinterpret_cast<const sockaddr_in *>(destination)->sin_port));
    traceEs1Datagram("sendto", destination, configuredLength, buf, len);
    int ret = sendto(s, buf, len, flags, destination, configuredLength);
    const int wsaError = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(wsaError);
    }
    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait)
    {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(destination);
        log_info("LLSENDTO fd=%d len=%d -> %d wsa=%d errno=%d dest=%s:%u", guestSocket, len, ret,
                 wsaError, ret == SOCKET_ERROR ? errno : 0,
                 destination && destination->sa_family == AF_INET ? inet_ntoa(ipv4->sin_addr) : "?",
                 destination && destination->sa_family == AF_INET ? ntohs(ipv4->sin_port) : 0u);
    }
    log_trace(">>> sendto EXIT: returning %d", ret);
    return ret;
}

static constexpr int linuxMsgDontWait = 0x40;
static constexpr int linuxMsgNoSignal = 0x4000;
static constexpr int linuxMsgWaitAll = 0x100;

static int translateMessageFlags(int flags)
{
    int translated = flags & ~(linuxMsgDontWait | linuxMsgNoSignal | linuxMsgWaitAll);
    if (flags & linuxMsgWaitAll)
        translated |= MSG_WAITALL;
    return translated;
}

// Winsock stores the length before the pointer, so the vectors must be rebuilt.
static bool buildWsaBuffers(const LinuxMsghdr *message, std::vector<WSABUF> &buffers)
{
    if (!message)
    {
        errno = EFAULT;
        return false;
    }

    if (message->iovCount && !message->iov)
    {
        errno = EFAULT;
        return false;
    }

    buffers.resize(message->iovCount);
    for (uint32_t i = 0; i < message->iovCount; ++i)
    {
        buffers[i].len = static_cast<ULONG>(message->iov[i].length);
        buffers[i].buf = static_cast<CHAR *>(message->iov[i].base);
    }
    return true;
}

extern "C" int bridgeSendmsg(SOCKET s, const LinuxMsghdr *message, int flags)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 sendmsg fd=%d iov=%u flags=0x%x name=%u", guestSocket,
                 message ? message->iovCount : 0, flags, message ? message->nameLength : 0);
    rememberEs1OutgoingPcb(message);
    std::vector<WSABUF> buffers;
    if (!buildWsaBuffers(message, buffers))
        return -1;

    if (message->controlLength)
    {
        // Only used for SCM_RIGHTS style ancillary data, which the LAN code
        // never sends; dropping it is safer than failing the whole transfer.
            log_debug(">>> sendmsg: dropping %u bytes of ancillary data",
                 static_cast<unsigned>(message->controlLength));
    }

    DWORD sent = 0;
    const DWORD wsaFlags = static_cast<DWORD>(translateMessageFlags(flags));
    sockaddr_storage configuredAddress = {};
    int configuredLength = message->nameLength;
    const struct sockaddr *messageDestination =
        static_cast<const struct sockaddr *>(message->name);
    const int messageDestinationLength = static_cast<int>(message->nameLength);
    /* As in the real cabinet LAN, multicast discovery is sent to the group
     * and is controlled by IP_MULTICAST_LOOP.  Only ordinary guest/host
     * addresses are translated to the host adapter below. */
    bool rewritten = !isEs1MulticastAddress(messageDestination, messageDestinationLength) &&
                     makeEs1MulticastAddress(messageDestination, messageDestinationLength,
                                             configuredAddress, configuredLength);
    if (!rewritten)
        rewritten = makeConfiguredBroadcastAddress(
        static_cast<const struct sockaddr *>(message->name), static_cast<int>(message->nameLength),
        configuredAddress, configuredLength);
    if (!rewritten)
        rewritten = makeEs1GuestAddress(static_cast<const struct sockaddr *>(message->name),
                                        static_cast<int>(message->nameLength),
                                        configuredAddress, configuredLength);
    const struct sockaddr *destination = rewritten
                                             ? reinterpret_cast<const struct sockaddr *>(&configuredAddress)
                                             : static_cast<const struct sockaddr *>(message->name);
    int ret;
    if (message->name && message->nameLength)
    {
        ret = WSASendTo(s, buffers.data(), static_cast<DWORD>(buffers.size()), &sent, wsaFlags,
                        destination, configuredLength, nullptr, nullptr);
        /* The cabinet LAN is a closed segment that is always reachable, so a
         * WSAEHOSTUNREACH here is our own routing choice lapsing on a host with
         * VPN or VM adapters.  Re-pin the interface and send once more; the
         * title drops the message otherwise. */
        if (ret == SOCKET_ERROR && WSAGetLastError() == WSAEHOSTUNREACH &&
            isEs1MulticastAddress(destination, configuredLength))
        {
            in_addr adapter = {};
            if (getHostIPv4(&adapter))
            {
                setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
                           reinterpret_cast<const char *>(&adapter), sizeof(adapter));
                ret = WSASendTo(s, buffers.data(), static_cast<DWORD>(buffers.size()), &sent,
                                wsaFlags, destination, configuredLength, nullptr, nullptr);
                static std::atomic<unsigned int> repinned{0};
                if (repinned.fetch_add(1) < 8)
                    log_warn("Network bridge: multicast send hit WSAEHOSTUNREACH; re-pinned the "
                             "interface and retried (%s)",
                             ret == SOCKET_ERROR ? "still failing" : "sent");
            }
        }
    }
    else
        ret = WSASend(s, buffers.data(), static_cast<DWORD>(buffers.size()), &sent, wsaFlags,
                      nullptr, nullptr);

    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait)
        log_info("LLSENDMSG fd=%d iov=%u sent=%lu ret=%d wsa=%d", guestSocket,
                 message->iovCount, static_cast<unsigned long>(sent), ret,
                 ret == SOCKET_ERROR ? WSAGetLastError() : 0);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> sendmsg EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
        if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
            log_debug("ES1 sendmsg result=-1 errno=%d wsa=%d", errno, WSAGetLastError());
        return -1;
    }

    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 sendmsg result=%d sent=%lu", ret, static_cast<unsigned long>(sent));

    log_trace(">>> sendmsg EXIT: socket=%lld sent %lu bytes in %u buffers",
             (long long)s, (unsigned long)sent, static_cast<unsigned>(buffers.size()));
    return static_cast<int>(sent);
}

extern "C" int bridgeRecvmsg(SOCKET s, LinuxMsghdr *message, int flags)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 recvmsg fd=%d iov=%u flags=0x%x name=%u", guestSocket,
                 message ? message->iovCount : 0, flags, message ? message->nameLength : 0);
    std::vector<WSABUF> buffers;
    if (!buildWsaBuffers(message, buffers))
        return -1;

    DWORD received = 0;
    DWORD wsaFlags = static_cast<DWORD>(translateMessageFlags(flags));
    int ret;
    if (message->name && message->nameLength)
    {
        int nameLength = static_cast<int>(message->nameLength);
        ret = WSARecvFrom(s, buffers.data(), static_cast<DWORD>(buffers.size()), &received, &wsaFlags,
                          static_cast<struct sockaddr *>(message->name), &nameLength, nullptr, nullptr);
        if (ret != SOCKET_ERROR)
        {
            message->nameLength = static_cast<uint32_t>(nameLength);
            const uint8_t *data = (message->iov && message->iovCount != 0)
                                      ? static_cast<const uint8_t *>(message->iov[0].base)
                                      : nullptr;
            const size_t dataLength = (message->iov && message->iovCount != 0)
                                           ? static_cast<size_t>(received)
                                           : 0;
            /* The clLanBuffer source field is part of the buffer checksum and
             * carries the logical multicast address, so only the outer UDP peer
             * address is virtualised below. */
            rewriteEs1MessagePeerAddress(static_cast<struct sockaddr *>(message->name), nameLength,
                                         data, dataLength);
            if (es1IsDetected() && data && dataLength >= 36 &&
                g_es1PacketContentTraceCount.fetch_add(1) < 32)
            {
                /* recvmsg() receives the complete clLanBuffer. */
                const uint32_t bufferType = *reinterpret_cast<const uint32_t *>(data + 0x14);
                const uint32_t bufferAddress = *reinterpret_cast<const uint32_t *>(data + 8);
                const auto *peer = reinterpret_cast<const sockaddr_in *>(message->name);
                log_debug("ES1 clLanBuffer len=%u type=0x%08x src=0x%08x peer=%u.%u.%u.%u",
                         static_cast<unsigned>(data[0] | (data[1] << 8)),
                         bufferType, bufferAddress,
                         peer->sin_addr.S_un.S_un_b.s_b1, peer->sin_addr.S_un.S_un_b.s_b2,
                         peer->sin_addr.S_un.S_un_b.s_b3, peer->sin_addr.S_un.S_un_b.s_b4);
            }
            if (es1IsDetected() && isEs1MessagePacket(data, dataLength) &&
                g_es1PacketContentTraceCount.fetch_add(1) < 64)
                log_debug("ES1 clLanBuffer rx len=%u src=%08x type=%08x payload=%02x %02x %02x %02x",
                         static_cast<unsigned>(data[0] | (data[1] << 8)),
                         *reinterpret_cast<const uint32_t *>(data + 8),
                         *reinterpret_cast<const uint32_t *>(data + 0x14),
                         data[0x18], data[0x19], data[0x1a], data[0x1b]);
        }
    }
    else
    {
        ret = WSARecv(s, buffers.data(), static_cast<DWORD>(buffers.size()), &received, &wsaFlags,
                      nullptr, nullptr);
    }

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> recvmsg EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
        if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
            log_debug("ES1 recvmsg result=-1 errno=%d wsa=%d", errno, WSAGetLastError());
        return -1;
    }

    if (es1IsDetected() && g_es1PacketResultTraceCount.fetch_add(1) < 128)
        log_debug("ES1 recvmsg result=%d received=%lu", ret, static_cast<unsigned long>(received));

    // Windows never reports ancillary data, so the caller must see none.
    message->controlLength = 0;
    message->flags = static_cast<int32_t>(wsaFlags);

    /* Same purpose as the recvfrom trace: the cabinet's LAN session is kept
     * alive by what arrives here, so a gap explains "local: session timeout".
     * The ES1 LAN code reads through recvmsg(), not recvfrom(). */
    static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
    if (traceWait && received > 0)
    {
        const auto *ipv4 = static_cast<const sockaddr_in *>(message->name);
        log_info("LLRECVMSG fd=%d bytes=%lu src=%s:%u", guestSocket,
                 static_cast<unsigned long>(received),
                 (message->name && ipv4->sin_family == AF_INET) ? inet_ntoa(ipv4->sin_addr) : "?",
                 (message->name && ipv4->sin_family == AF_INET) ? ntohs(ipv4->sin_port) : 0u);
    }

    log_trace(">>> recvmsg EXIT: socket=%lld received %lu bytes in %u buffers",
             (long long)s, (unsigned long)received, static_cast<unsigned>(buffers.size()));
    return static_cast<int>(received);
}

extern "C" int bridgeGetpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    int ret = getpeername(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> getpeername EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
    }
    if (ret == 0 && es1IsDetected() && name && namelen &&
        *namelen >= static_cast<int>(sizeof(sockaddr_in)) && name->sa_family == AF_INET)
    {
        unsigned char adapterBytes[4] = {};
        unsigned char guestBytes[4] = {};
        if (es1HostAdapterAddress(adapterBytes) && es1HostGuestAddress(guestBytes))
        {
            in_addr adapterAddress = {};
            in_addr guestAddress = {};
            std::memcpy(&adapterAddress.s_addr, adapterBytes, sizeof(adapterBytes));
            std::memcpy(&guestAddress.s_addr, guestBytes, sizeof(guestBytes));
            auto *peer = reinterpret_cast<sockaddr_in *>(name);
            if (peer->sin_addr.s_addr == adapterAddress.s_addr)
                peer->sin_addr = guestAddress;
        }
    }
    return ret;
}

extern "C" int bridgeGetsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    int ret = getsockname(s, name, namelen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        log_trace(">>> getsockname EXIT: returning -1 (WSAError=%d)", WSAGetLastError());
    }
    return ret;
}

namespace
{
constexpr int linuxSolSocket = 1;
constexpr int linuxIpprotoIp = 0;
constexpr int linuxIpprotoTcp = 6;

constexpr int linuxSoLinger = 13;
constexpr int linuxSoRcvtimeo = 20;
constexpr int linuxSoSndtimeo = 21;

struct OptionMapping
{
    int guest;
    int host;
};

const OptionMapping socketLevelOptions[] = {
    {1, SO_DEBUG},      {2, SO_REUSEADDR}, {3, SO_TYPE},       {4, SO_ERROR},
    {5, SO_DONTROUTE},  {6, SO_BROADCAST}, {7, SO_SNDBUF},     {8, SO_RCVBUF},
    {9, SO_KEEPALIVE},  {10, SO_OOBINLINE}, {13, SO_LINGER},   {18, SO_RCVLOWAT},
    {19, SO_SNDLOWAT},  {20, SO_RCVTIMEO}, {21, SO_SNDTIMEO},  {30, SO_ACCEPTCONN},
};

const OptionMapping ipLevelOptions[] = {
    {1, IP_TOS},           {2, IP_TTL},
    {32, IP_MULTICAST_IF}, {33, IP_MULTICAST_TTL},
    {34, IP_MULTICAST_LOOP}, {35, IP_ADD_MEMBERSHIP},
    {36, IP_DROP_MEMBERSHIP},
};

bool translateOption(int &level, int &optname)
{
    if (level == linuxSolSocket)
    {
        for (const OptionMapping &mapping : socketLevelOptions)
        {
            if (mapping.guest != optname)
                continue;
            level = SOL_SOCKET;
            optname = mapping.host;
            return true;
        }
        log_warn("Network bridge: no Winsock equivalent for SOL_SOCKET option %d", optname);
        return false;
    }

    if (level == linuxIpprotoIp)
    {
        for (const OptionMapping &mapping : ipLevelOptions)
        {
            if (mapping.guest != optname)
                continue;
            optname = mapping.host;
            return true;
        }
        log_warn("Network bridge: no Winsock equivalent for IPPROTO_IP option %d", optname);
        return false;
    }

    // IPPROTO_TCP and its TCP_NODELAY happen to agree on both sides.
    if (level == linuxIpprotoTcp)
        return true;

    log_warn("Network bridge: unknown socket option level %d", level);
    return false;
}

struct LinuxLinger
{
    int32_t onOff;
    int32_t seconds;
};

struct LinuxTimeval
{
    int32_t seconds;
    int32_t microseconds;
};

bool useHostMulticastInterface(in_addr &interfaceAddress)
{
    if (!n2IsWanganTitle() && !es1IsDetected())
        return false;

    in_addr hostAddress = {};
    if (!getHostIPv4(&hostAddress))
        return false;

    interfaceAddress = hostAddress;
    return true;
}
} // namespace

extern "C" int bridgeSetsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    if (es1IsDetected() && g_es1PacketTraceCount.fetch_add(1) < 96)
        log_debug("ES1 setsockopt fd=%d level=%d opt=%d len=%d", guestSocket, level, optname, optlen);
    log_trace(">>> setsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    const int guestLevel = level;
    const int guestOption = optname;
    if (!translateOption(level, optname))
    {
        errno = ENOPROTOOPT;
        return -1;
    }

    int ret;
    if (guestLevel == linuxSolSocket && guestOption == linuxSoLinger &&
        optval && optlen >= static_cast<int>(sizeof(LinuxLinger)))
    {
        // Linux uses two ints, Winsock two shorts.
        const LinuxLinger *guest = reinterpret_cast<const LinuxLinger *>(optval);
        struct linger host = {};
        host.l_onoff = static_cast<u_short>(guest->onOff);
        host.l_linger = static_cast<u_short>(guest->seconds);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&host), sizeof(host));
    }
    else if (guestLevel == linuxIpprotoIp &&
             (guestOption == 35 || guestOption == 36) &&
             optval && optlen >= static_cast<int>(sizeof(in_addr) * 2))
    {
        struct ip_mreq host = {};
        std::memcpy(&host.imr_multiaddr, optval, sizeof(host.imr_multiaddr));
        std::memcpy(&host.imr_interface, optval + sizeof(host.imr_multiaddr),
                    sizeof(host.imr_interface));
        useHostMulticastInterface(host.imr_interface);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&host), sizeof(host));
    }
    else if (guestLevel == linuxIpprotoIp && guestOption == 32 &&
             optval && optlen >= static_cast<int>(sizeof(in_addr)))
    {
        in_addr host = {};
        std::memcpy(&host, optval, sizeof(host));
        useHostMulticastInterface(host);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&host), sizeof(host));
    }
    else if (guestLevel == linuxSolSocket &&
             (guestOption == linuxSoRcvtimeo || guestOption == linuxSoSndtimeo) &&
             optval && optlen >= static_cast<int>(sizeof(LinuxTimeval)))
    {
        // Linux takes a timeval, Winsock a millisecond count.
        const LinuxTimeval *guest = reinterpret_cast<const LinuxTimeval *>(optval);
        DWORD milliseconds = static_cast<DWORD>(guest->seconds) * 1000u +
                             static_cast<DWORD>(guest->microseconds / 1000);
        ret = setsockopt(s, level, optname, reinterpret_cast<const char *>(&milliseconds),
                         sizeof(milliseconds));
    }
    else
    {
        ret = setsockopt(s, level, optname, optval, optlen);
    }

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

extern "C" int bridgeGetsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
    const int guestSocket = static_cast<int>(s);
    s = NetworkBridge::hostSocket(static_cast<int>(s));
    log_trace(">>> getsockopt called: socket=%lld, level=%d, optname=%d", (long long)s, level, optname);

    const int guestLevel = level;
    const int guestOption = optname;
    if (!translateOption(level, optname))
    {
        errno = ENOPROTOOPT;
        return -1;
    }

    if (guestLevel == linuxSolSocket && guestOption == linuxSoLinger &&
        optval && optlen && *optlen >= static_cast<int>(sizeof(LinuxLinger)))
    {
        struct linger host = {};
        int hostLength = sizeof(host);
        const int ret = getsockopt(s, level, optname, reinterpret_cast<char *>(&host), &hostLength);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        LinuxLinger *guest = reinterpret_cast<LinuxLinger *>(optval);
        guest->onOff = host.l_onoff;
        guest->seconds = host.l_linger;
        *optlen = sizeof(LinuxLinger);
        return 0;
    }

    if (guestLevel == linuxSolSocket &&
        (guestOption == linuxSoRcvtimeo || guestOption == linuxSoSndtimeo) &&
        optval && optlen && *optlen >= static_cast<int>(sizeof(LinuxTimeval)))
    {
        DWORD milliseconds = 0;
        int hostLength = sizeof(milliseconds);
        const int ret = getsockopt(s, level, optname, reinterpret_cast<char *>(&milliseconds), &hostLength);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        LinuxTimeval *guest = reinterpret_cast<LinuxTimeval *>(optval);
        guest->seconds = static_cast<int32_t>(milliseconds / 1000u);
        guest->microseconds = static_cast<int32_t>((milliseconds % 1000u) * 1000u);
        *optlen = sizeof(LinuxTimeval);
        return 0;
    }

    const int ret = getsockopt(s, level, optname, optval, optlen);
    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return ret;
    }

    /* SO_ERROR carries a WSA code, and the guest reads it as an errno, so it
     * needs the same translation as every other error crossing here. */
    if (guestLevel == linuxSolSocket && guestOption == 4 /* SO_ERROR */ &&
        optval && optlen && *optlen >= static_cast<int>(sizeof(int)))
    {
        int pending = 0;
        std::memcpy(&pending, optval, sizeof(pending));
        const int translated = pending ? mapWSAErrorToErrno(pending) : 0;
        std::memcpy(optval, &translated, sizeof(translated));
        static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
        if (traceWait)
            log_info("LLSOERR fd=%d wsa=%d -> errno=%d", guestSocket, pending, translated);
        else if (es1IsDetected() && pending &&
                 g_es1PacketResultTraceCount.fetch_add(1) < 128)
            log_debug("Network bridge: SO_ERROR wsa=%d -> errno=%d", pending, translated);
    }
    return ret;
}

/* FIONBIO and FIONREAD carry the same numbers on both systems, but they have
 * to reach ioctlsocket() rather than the CRT's file ioctl. */
extern "C" int bridgeSocketIoctl(int descriptor, unsigned long request, void *argument)
{
    const SOCKET handle = NetworkBridge::hostSocket(descriptor);
    constexpr unsigned long linuxFionread = 0x541B;
    constexpr unsigned long linuxFionbio = 0x5421;

    u_long value = 0;
    if (request == linuxFionbio || request == static_cast<unsigned long>(FIONBIO))
    {
        value = argument ? static_cast<u_long>(*static_cast<const int *>(argument)) : 0;
        const int ret = ioctlsocket(handle, FIONBIO, &value);
        if (ret == SOCKET_ERROR)
            errno = mapWSAErrorToErrno(WSAGetLastError());
        return ret;
    }

    if (request == linuxFionread || request == static_cast<unsigned long>(FIONREAD))
    {
        const int ret = ioctlsocket(handle, FIONREAD, &value);
        if (ret == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return ret;
        }
        if (argument)
            *static_cast<int *>(argument) = static_cast<int>(value);
        return 0;
    }

    /* SIOCGIF*, the pre-getifaddrs interface query ES1's network diagnostic
     * uses.  The argument is a struct ifreq, and the title polls it every
     * frame, so an unhandled request both fails the check and floods the log. */
    constexpr unsigned long siocGifaddr = 0x8915;
    constexpr unsigned long siocGifflags = 0x8913;
    constexpr unsigned long siocGifnetmask = 0x891b;
    constexpr unsigned long siocGifbrdaddr = 0x8919;
    constexpr unsigned long siocGifhwaddr = 0x8927;
    constexpr unsigned long siocGifindex = 0x8933;

    if (request == siocGifaddr || request == siocGifflags || request == siocGifnetmask ||
        request == siocGifbrdaddr || request == siocGifhwaddr || request == siocGifindex)
    {
        if (!argument)
        {
            errno = EFAULT;
            return -1;
        }

        auto *ifreq = static_cast<unsigned char *>(argument);
        const char *name = reinterpret_cast<const char *>(ifreq);
        const bool loopback = std::strncmp(name, "lo", 2) == 0 && (name[2] == '\0');

        unsigned long address = 0;
        unsigned long netmask = 0;
        unsigned char mac[6]{};
        NetworkBridge::primaryHostInterface(address, netmask, mac);

        if (loopback)
        {
            address = htonl(INADDR_LOOPBACK);
            netmask = htonl(0xFF000000u);
        }
        else if (address == 0)
        {
            errno = ENODEV;
            return -1;
        }

        /* The union starts right after the 16-byte name. */
        unsigned char *value = ifreq + 16;
        std::memset(value, 0, 16);

        if (request == siocGifflags)
        {
            const unsigned short flags =
                loopback ? (0x1 | 0x8 | 0x40)      /* UP | LOOPBACK | RUNNING */
                         : (0x1 | 0x2 | 0x40 | 0x1000); /* UP | BROADCAST | RUNNING | MULTICAST */
            std::memcpy(value, &flags, sizeof(flags));
            return 0;
        }

        if (request == siocGifindex)
        {
            const int index = loopback ? 1 : 2;
            std::memcpy(value, &index, sizeof(index));
            return 0;
        }

        if (request == siocGifhwaddr)
        {
            /* sa_family then 14 bytes of address; ARPHRD_ETHER / ARPHRD_LOOPBACK. */
            const unsigned short family = loopback ? 772 : 1;
            std::memcpy(value, &family, sizeof(family));
            if (!loopback)
                std::memcpy(value + 2, mac, sizeof(mac));
            return 0;
        }

        sockaddr_in result{};
        result.sin_family = AF_INET;
        if (request == siocGifaddr)
            result.sin_addr.s_addr = address;
        else if (request == siocGifnetmask)
            result.sin_addr.s_addr = netmask;
        else
            result.sin_addr.s_addr = (address & netmask) | ~netmask;
        std::memcpy(value, &result, sizeof(result));
        return 0;
    }

    /* An unhandled request usually repeats every frame; say so once per kind
     * rather than filling the log. */
    static HostMutex reportedMutex;
    static std::unordered_set<unsigned long> reported;
    {
        std::lock_guard<HostMutex> lock(reportedMutex);
        if (reported.insert(request).second)
            log_warn("Network bridge: unsupported socket ioctl 0x%lx", request);
    }
    errno = EINVAL;
    return -1;
}

static short linuxPollEventsToWinsock(short events)
{
    short translated = 0;
    if (events & 0x001) // POLLIN
        translated |= POLLRDNORM;
    if (events & 0x002) // POLLPRI
        translated |= POLLRDBAND;
    if (events & 0x004) // POLLOUT
        translated |= POLLWRNORM;
    return translated;
}

static short winsockPollEventsToLinux(short events)
{
    short translated = 0;
    if (events & POLLRDNORM)
        translated |= 0x001; // POLLIN
    if (events & POLLRDBAND)
        translated |= 0x002; // POLLPRI
    if (events & (POLLWRNORM | POLLWRBAND))
        translated |= 0x004; // POLLOUT
    if (events & POLLERR)
        translated |= 0x008; // POLLERR
    if (events & POLLHUP)
        translated |= 0x010; // POLLHUP
    if (events & POLLNVAL)
        translated |= 0x020; // POLLNVAL
    return translated;
}

extern "C" int NetworkBridge::bridgePoll(void *rawFds, int nfds, int timeout)
{
    if (nfds < 0 || (nfds > 0 && !rawFds))
    {
        errno = EINVAL;
        return -1;
    }

    auto *guestFds = static_cast<LinuxPollfd *>(rawFds);
    std::vector<WSAPOLLFD> hostFds;
    std::vector<int> guestIndexes;
    hostFds.reserve(static_cast<size_t>(nfds));
    guestIndexes.reserve(static_cast<size_t>(nfds));

    int invalidCount = 0;
    int virtualReadyCount = 0;
    for (int i = 0; i < nfds; i++)
    {
        guestFds[i].revents = 0;
        if (guestFds[i].fd < 0)
            continue;

        if (!NetworkBridge::isSocketDescriptor(guestFds[i].fd))
        {
            if (const auto *device = VirtualDeviceRegistry::find(guestFds[i].fd))
            {
                if ((guestFds[i].events & 0x001) &&
                    device->bytesAvailable(guestFds[i].fd) > 0)
                    guestFds[i].revents |= 0x001; // POLLIN
                if (guestFds[i].events & 0x004)
                    guestFds[i].revents |= 0x004; // POLLOUT
                if (guestFds[i].revents != 0)
                    virtualReadyCount++;
            }
            else
            {
                guestFds[i].revents = 0x020; // POLLNVAL
                invalidCount++;
            }
            continue;
        }

        WSAPOLLFD host = {};
        host.fd = NetworkBridge::hostSocket(guestFds[i].fd);
        host.events = linuxPollEventsToWinsock(guestFds[i].events);
        host.revents = 0;
        hostFds.push_back(host);
        guestIndexes.push_back(i);
    }

    if (hostFds.empty())
    {
        if (virtualReadyCount == 0 && invalidCount == 0 && timeout != 0)
        {
            /* Virtual devices have no host wait handle.  Poll in short
             * intervals so serial responses and camera frames can become
             * visible without turning a blocking guest poll into a spin. */
            const int waitMilliseconds = timeout < 0 ? 10 : std::min(timeout, 10);
            Sleep(static_cast<DWORD>(waitMilliseconds));
        }
        return invalidCount + virtualReadyCount;
    }

    const int hostTimeout = virtualReadyCount != 0 ? 0 : timeout;
    const int ready = WSAPoll(hostFds.data(), static_cast<ULONG>(hostFds.size()), hostTimeout);
    if (ready == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }

    int resultCount = invalidCount + virtualReadyCount;
    for (size_t i = 0; i < hostFds.size(); i++)
    {
        const short revents = winsockPollEventsToLinux(hostFds[i].revents);
        guestFds[guestIndexes[i]].revents = revents;
        if (revents != 0)
            resultCount++;
        /* LL_WAIT_TRACE=1: the writable side of poll() is what carries an async
         * connect to completion, so show those calls and what came back. */
        static const bool traceWait = std::getenv("LL_WAIT_TRACE") != nullptr;
        if (traceWait)
            log_info("LLPOLL fd=%d events=0x%x hostEvents=0x%x hostRevents=0x%x revents=0x%x timeout=%d",
                     guestFds[guestIndexes[i]].fd,
                     static_cast<unsigned>(guestFds[guestIndexes[i]].events),
                     static_cast<unsigned>(hostFds[i].events),
                     static_cast<unsigned>(hostFds[i].revents),
                     static_cast<unsigned>(revents), timeout);
    }

    log_trace("Network bridge: poll nfds=%d timeout=%d ready=%d", nfds, timeout, resultCount);
    return resultCount;
}

namespace
{
constexpr int guestFdSetBits = 1024;
constexpr size_t guestFdSetBytes = guestFdSetBits / 8;

void guestFdSet(int descriptor, void *set)
{
    if (!set || descriptor < 0 || descriptor >= guestFdSetBits)
        return;
    uint32_t *words = static_cast<uint32_t *>(set);
    words[descriptor / 32] |= (1u << (descriptor % 32));
}

void guestFdZero(void *set)
{
    if (set)
        memset(set, 0, guestFdSetBytes);
}
} // namespace

extern "C" int bridgeSelectDescriptors(int nfds, void *readSet, void *writeSet, void *exceptSet,
                                       void *timeoutArgument)
{
    fd_set hostRead;
    fd_set hostWrite;
    fd_set hostExcept;
    FD_ZERO(&hostRead);
    FD_ZERO(&hostWrite);
    FD_ZERO(&hostExcept);

    int watchedSockets = 0;
    int ignoredDescriptors = 0;
    std::vector<int> readyVirtualRead;
    std::vector<int> readyVirtualWrite;

    const int limit = nfds < guestFdSetBits ? nfds : guestFdSetBits;
    const uint32_t *readWords = static_cast<const uint32_t *>(readSet);
    const uint32_t *writeWords = static_cast<const uint32_t *>(writeSet);
    const uint32_t *exceptWords = static_cast<const uint32_t *>(exceptSet);

    for (int word = 0; word * 32 < limit; word++)
    {
        const uint32_t readBits = readWords ? readWords[word] : 0;
        const uint32_t writeBits = writeWords ? writeWords[word] : 0;
        const uint32_t exceptBits = exceptWords ? exceptWords[word] : 0;
        uint32_t anyBits = readBits | writeBits | exceptBits;
        if (!anyBits)
            continue;

        while (anyBits)
        {
            const int bit = __builtin_ctz(anyBits);
            anyBits &= anyBits - 1;

            const int descriptor = word * 32 + bit;
            if (descriptor >= limit)
                break;

            if (!NetworkBridge::isSocketDescriptor(descriptor))
            {
                if (const auto *device = VirtualDeviceRegistry::find(descriptor))
                {
                    const uint32_t mask = 1u << bit;
                    if ((readBits & mask) && device->bytesAvailable(descriptor) > 0)
                        readyVirtualRead.push_back(descriptor);
                    if (writeBits & mask)
                        readyVirtualWrite.push_back(descriptor);
                }
                else
                {
                    ignoredDescriptors++;
                }
                continue;
            }

            const SOCKET handle = NetworkBridge::hostSocket(descriptor);
            const uint32_t mask = 1u << bit;
            if (readBits & mask)
                FD_SET(handle, &hostRead);
            if (writeBits & mask)
                FD_SET(handle, &hostWrite);
            if (exceptBits & mask)
                FD_SET(handle, &hostExcept);
            watchedSockets++;
        }
    }

    if (ignoredDescriptors)
        log_trace("Network bridge: select() skipped %d non-socket descriptor(s)", ignoredDescriptors);

    const LinuxTimeval *guestTimeout = static_cast<const LinuxTimeval *>(timeoutArgument);

    int selected = 0;
    if (watchedSockets != 0)
    {
        TIMEVAL hostTimeout = {};
        TIMEVAL *hostTimeoutPointer = nullptr;
        if (!readyVirtualRead.empty() || !readyVirtualWrite.empty())
        {
            hostTimeoutPointer = &hostTimeout;
        }
        else if (guestTimeout)
        {
            hostTimeout.tv_sec = guestTimeout->seconds;
            hostTimeout.tv_usec = guestTimeout->microseconds;
            hostTimeoutPointer = &hostTimeout;
        }

        selected = select(0, &hostRead, &hostWrite, &hostExcept, hostTimeoutPointer);
        if (selected == SOCKET_ERROR)
        {
            errno = mapWSAErrorToErrno(WSAGetLastError());
            return -1;
        }
    }
    else if (readyVirtualRead.empty() && readyVirtualWrite.empty() && guestTimeout)
    {
        // Winsock refuses a select() with nothing in it, so a wait on no
        // sockets is served by sleeping the caller's timeout out.
        Sleep(static_cast<DWORD>(guestTimeout->seconds) * 1000u +
              static_cast<DWORD>(guestTimeout->microseconds / 1000));
    }

    guestFdZero(readSet);
    guestFdZero(writeSet);
    guestFdZero(exceptSet);

    // POSIX counts every ready (descriptor, set) pair, not every descriptor.
    int count = 0;
    for (const int descriptor : readyVirtualRead)
    {
        guestFdSet(descriptor, readSet);
        count++;
    }
    for (const int descriptor : readyVirtualWrite)
    {
        guestFdSet(descriptor, writeSet);
        count++;
    }
    if (selected > 0)
    {
        for (u_int i = 0; i < hostRead.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostRead.fd_array[i]), readSet);
            count++;
        }
        for (u_int i = 0; i < hostWrite.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostWrite.fd_array[i]), writeSet);
            count++;
        }
        for (u_int i = 0; i < hostExcept.fd_count; i++)
        {
            guestFdSet(NetworkBridge::guestDescriptor(hostExcept.fd_array[i]), exceptSet);
            count++;
        }
    }

    return count;
}

extern "C" int bridgeSocketPair(int descriptors[2])
{
    if (!descriptors)
    {
        errno = EINVAL;
        return -1;
    }

    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;

    auto fail = [&]() {
        const int error = WSAGetLastError();
        if (listener != INVALID_SOCKET)
            closesocket(listener);
        if (client != INVALID_SOCKET)
            closesocket(client);
        if (server != INVALID_SOCKET)
            closesocket(server);
        errno = mapWSAErrorToErrno(error);
        log_error("Network bridge: could not build a loopback pipe pair (WSAError=%d)", error);
        return -1;
    };

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        return fail();

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR)
        return fail();

    int length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == SOCKET_ERROR)
        return fail();

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
        return fail();
    if (connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR)
        return fail();

    server = accept(listener, nullptr, nullptr);
    if (server == INVALID_SOCKET)
        return fail();

    closesocket(listener);
    listener = INVALID_SOCKET;

    // The wakeup is a single byte; Nagle must not sit on it.
    const int enabled = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled), sizeof(enabled));
    setsockopt(server, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled), sizeof(enabled));

    const int readEnd = NetworkBridge::registerSocket(server);
    const int writeEnd = NetworkBridge::registerSocket(client);
    if (readEnd < 0 || writeEnd < 0)
    {
        NetworkBridge::forgetSocket(readEnd);
        NetworkBridge::forgetSocket(writeEnd);
        closesocket(server);
        closesocket(client);
        errno = EMFILE;
        return -1;
    }

    descriptors[0] = readEnd;
    descriptors[1] = writeEnd;
    return 0;
}

namespace
{
struct GuestAddrinfoNode
{
    LinuxAddrinfo info = {};
    sockaddr_storage address = {};
    char canonicalName[NI_MAXHOST] = {};
};

HostMutex g_addrinfo_mutex;
std::unordered_set<LinuxAddrinfo *> g_addrinfo_nodes;

void freeGuestAddrinfoChain(LinuxAddrinfo *result)
{
    std::lock_guard<HostMutex> lock(g_addrinfo_mutex);
    while (result)
    {
        auto found = g_addrinfo_nodes.find(result);
        if (found == g_addrinfo_nodes.end())
        {
            log_warn("Network bridge: freeaddrinfo received an unknown record");
            return;
        }

        LinuxAddrinfo *next = result->aiNext;
        g_addrinfo_nodes.erase(found);
        delete reinterpret_cast<GuestAddrinfoNode *>(result);
        result = next;
    }
}
} // namespace

extern "C" int NetworkBridge::bridgeGetaddrinfo(const char *node, const char *service,
                                                 const LinuxAddrinfo *hints, LinuxAddrinfo **result)
{
    if (!result)
        return EAI_FAIL;
    *result = nullptr;

    struct addrinfo hostHints = {};
    struct addrinfo *hostHintsPointer = nullptr;
    if (hints)
    {
        hostHints.ai_flags = hints->aiFlags;
        hostHints.ai_family = hints->aiFamily;
        hostHints.ai_socktype = hints->aiSocktype;
        hostHints.ai_protocol = hints->aiProtocol;
        hostHintsPointer = &hostHints;
    }

    struct addrinfo *hostResult = nullptr;
    const char *lookupNode = wmmt4RedirectedHost(node);
    const int error = getaddrinfo(lookupNode, service, hostHintsPointer, &hostResult);
    if (error != 0)
        return error;

    LinuxAddrinfo *first = nullptr;
    LinuxAddrinfo *previous = nullptr;
    for (const struct addrinfo *host = hostResult; host; host = host->ai_next)
    {
        GuestAddrinfoNode *guest = new (std::nothrow) GuestAddrinfoNode();
        if (!guest)
        {
            freeaddrinfo(hostResult);
            freeGuestAddrinfoChain(first);
            return EAI_MEMORY;
        }

        guest->info.aiFlags = host->ai_flags;
        guest->info.aiFamily = host->ai_family;
        guest->info.aiSocktype = host->ai_socktype;
        guest->info.aiProtocol = host->ai_protocol;
        guest->info.aiAddrlen = static_cast<uint32_t>(std::min<size_t>(
            host->ai_addrlen, sizeof(guest->address)));
        if (host->ai_addr && guest->info.aiAddrlen)
        {
            std::memcpy(&guest->address, host->ai_addr, guest->info.aiAddrlen);
            guest->info.aiAddr = reinterpret_cast<struct sockaddr *>(&guest->address);
        }
        if (host->ai_canonname)
        {
            std::strncpy(guest->canonicalName, host->ai_canonname,
                         sizeof(guest->canonicalName) - 1);
            guest->info.aiCanonname = guest->canonicalName;
        }

        if (!first)
            first = &guest->info;
        if (previous)
            previous->aiNext = &guest->info;
        previous = &guest->info;

        std::lock_guard<HostMutex> lock(g_addrinfo_mutex);
        g_addrinfo_nodes.insert(&guest->info);
    }

    freeaddrinfo(hostResult);
    *result = first;
    return 0;
}

extern "C" void NetworkBridge::bridgeFreeaddrinfo(LinuxAddrinfo *result)
{
    freeGuestAddrinfoChain(result);
}

extern "C" unsigned int NetworkBridge::bridgeIf_nametoindex(const char *name)
{
    if (!name)
    {
        errno = EINVAL;
        return 0;
    }

    if (_stricmp(name, "eth0") == 0)
    {
        in_addr address = {};
        if (!getHostIPv4(&address))
        {
            errno = ENODEV;
            return 0;
        }
        log_debug("Network bridge: if_nametoindex(%s) -> 2", name);
        return 2;
    }
    if (_stricmp(name, "lo") == 0)
    {
        log_debug("Network bridge: if_nametoindex(%s) -> 1", name);
        return 1;
    }

    errno = ENODEV;
    return 0;
}

extern "C" char *NetworkBridge::bridgeIf_indextoname(unsigned int index, char *name)
{
    if (!name || (index != 0 && index != 1 && index != 2))
    {
        errno = EINVAL;
        return nullptr;
    }

    std::strcpy(name, index == 1 ? "lo" : "eth0");
    log_debug("Network bridge: if_indextoname(%u) -> %s", index, name);
    return name;
}

#pragma pack(push, 4)
struct LinuxHostent
{
    char *h_name;
    char **h_aliases;
    int32_t h_addrtype;
    int32_t h_length;
    char **h_addr_list;
};
#pragma pack(pop)
static_assert(sizeof(struct LinuxHostent) == 20, "i386 struct hostent is 20 bytes");

namespace
{
    // One record per thread, the same lifetime rule the real gethostbyname has.
    struct HostentStorage
    {
        LinuxHostent record;
        char name[256];
        uint8_t address[4];
        char *addressList[2];
        char *aliasList[1];
    };

    LinuxHostent *resolveHost(const char *name)
    {
        static thread_local HostentStorage storage;

        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo *found = nullptr;
        const char *lookupName = wmmt4RedirectedHost(name);
        if (getaddrinfo(lookupName, nullptr, &hints, &found) != 0 || !found)
            return nullptr;

        const struct sockaddr_in *resolved = reinterpret_cast<struct sockaddr_in *>(found->ai_addr);
        memcpy(storage.address, &resolved->sin_addr, sizeof(storage.address));
        strncpy(storage.name, name, sizeof(storage.name) - 1);
        storage.name[sizeof(storage.name) - 1] = '\0';
        freeaddrinfo(found);

        storage.addressList[0] = reinterpret_cast<char *>(storage.address);
        storage.addressList[1] = nullptr;
        storage.aliasList[0] = nullptr;

        storage.record.h_name = storage.name;
        storage.record.h_aliases = storage.aliasList;
        storage.record.h_addrtype = AF_INET;
        storage.record.h_length = static_cast<int32_t>(sizeof(storage.address));
        storage.record.h_addr_list = storage.addressList;
        return &storage.record;
    }
}

extern "C" void *bridgeGethostbyname(const char *name)
{
    std::lock_guard<HostMutex> lock(g_net_mutex);

    LinuxHostent *record = name ? resolveHost(name) : nullptr;
    if (!record)
    {
        log_debug("gethostbyname(\"%s\") found no address", name ? name : "(null)");
        return nullptr;
    }

    log_trace("gethostbyname(\"%s\") = %u.%u.%u.%u", name,
              (unsigned)(uint8_t)record->h_addr_list[0][0], (unsigned)(uint8_t)record->h_addr_list[0][1],
              (unsigned)(uint8_t)record->h_addr_list[0][2], (unsigned)(uint8_t)record->h_addr_list[0][3]);
    return record;
}

int NetworkBridge::bridgeGethostbyname_r(const char *name, void *ret, char *buf, size_t buflen, void **result, int *h_errnop)
{
    std::lock_guard<HostMutex> lock(g_net_mutex);

    LinuxHostent *record = name ? resolveHost(name) : nullptr;
    if (!record)
    {
        if (h_errnop)
            *h_errnop = 1; // HOST_NOT_FOUND
        if (result)
            *result = nullptr;
        return -1;
    }

    if (ret)
        memcpy(ret, record, sizeof(*record));
    if (result)
        *result = ret ? ret : record;
    return 0;
}

extern "C" void *bridgeGethostbyaddr(const void *addr, int len, int type)
{
    std::lock_guard<HostMutex> lock(g_net_mutex);

    if (!addr || len != 4 || type != AF_INET)
        return nullptr;

    char text[INET_ADDRSTRLEN] = {};
    if (!InetNtopA(AF_INET, const_cast<void *>(addr), text, sizeof(text)))
        return nullptr;

    return resolveHost(text);
}

/* Same shape problem as hostent: Winsock's servent holds the port in a short
 * where i386 glibc holds an int, so the proto pointer moves. */
#pragma pack(push, 4)
struct LinuxServent
{
    char *s_name;
    char **s_aliases;
    int32_t s_port;
    char *s_proto;
};
#pragma pack(pop)
static_assert(sizeof(struct LinuxServent) == 16, "i386 struct servent is 16 bytes");

extern "C" void *bridgeGetservbyname(const char *name, const char *proto)
{
    std::lock_guard<HostMutex> lock(g_net_mutex);

    struct servent *entry = name ? getservbyname(name, proto) : nullptr;
    if (!entry)
        return nullptr;

    static thread_local LinuxServent record;
    static thread_local char *aliasList[1];
    aliasList[0] = nullptr;

    record.s_name = entry->s_name;
    record.s_aliases = aliasList;
    record.s_port = static_cast<int32_t>(static_cast<uint16_t>(entry->s_port));
    record.s_proto = entry->s_proto;
    return &record;
}

int NetworkBridge::bridgeGethostbyaddr_r(const void *addr, int len, int type, void *ret, char *buf, size_t buflen, void **result,
                                         int *h_errnop)
{
    std::lock_guard<HostMutex> lock(g_net_mutex);
    struct hostent *he = gethostbyaddr((const char *)addr, len, type);
    if (!he)
    {
        if (h_errnop)
            *h_errnop = WSAGetLastError();
        if (result)
            *result = nullptr;
        return -1;
    }
    memcpy(ret, he, sizeof(struct hostent));
    if (result)
        *result = ret;
    return 0;
}

extern "C" int bridgeGethostname(char *name, size_t namelen)
{
    if (name == nullptr || namelen == 0)
        return -1;

    if (gethostname(name, static_cast<int>(namelen)) == 0)
    {
        name[namelen - 1] = '\0';
        return 0;
    }

    strncpy(name, "localhost", namelen - 1);
    name[namelen - 1] = '\0';
    return 0;
}

#endif
