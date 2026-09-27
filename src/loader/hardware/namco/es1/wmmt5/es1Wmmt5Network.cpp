#include "es1Wmmt5Network.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>
#include <cstring>
#include <windows.h>

#include "es1Wmmt5Build.hpp"
#include "../es1CompatLayer.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* Answers for the live network WMN5r reads through before it draws anything;
 * without them its network objects stay null and it faults on one. */
namespace
{

/* Each signature runs to the end of its own prologue. */
constexpr uint8_t ContentRouterSignature[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x5d, 0x8b, 0x00};
constexpr uint8_t NetworkStateSignature[] = {0x55, 0x89, 0xe5, 0x56, 0x53, 0x83, 0xec, 0x10};
constexpr uint8_t LinkCheckSignature[] = {0x55, 0x89, 0xe5, 0x53, 0x83, 0xec, 0x04, 0x8b, 0x45, 0x08};
constexpr uint8_t PeerCheckSignature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x89, 0x5d, 0xf8};
constexpr uint8_t BillingSaveSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec};
constexpr uint8_t DecryptTokenSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0x3c, 0x14};

/* Two jumps decide whether a locally supplied content router is accepted;
 * there is no function boundary to hook, so they are patched. */
constexpr uint8_t RouterAcceptExpected[] = {0x0f, 0x84};
constexpr uint8_t RouterAcceptPatched[] = {0x0f, 0x85};
constexpr uint8_t RouterRejectExpected[] = {0x0f, 0x85};
constexpr uint8_t RouterRejectPatched[] = {0x0f, 0x84};

/* The live hostname in .rodata, replaced with one that resolves locally. */
constexpr char MuchaHostExpected[] = "v388-front.mucha-prd.nbgi-amnet.jp";
constexpr char MuchaHostLocal[] = "mucha.local";

int (*g_originalDecryptToken)(char *, int *, char *, void *) = nullptr;
void *g_originalNetworkState = nullptr;

/* The cabinet's own subnet; .254 is where the router sits on an ES1 network. */
uint32_t contentRouterAddress(void)
{
    return 0xfe5ca8c0u; /* 192.168.92.254, network byte order */
}

int wmmt5ContentRouter(void) { return static_cast<int>(contentRouterAddress()); }

/* The status of the cabinet link as an error_code, returned through a hidden
 * pointer the callee pops (ret 4); the object is the second stack argument.
 * The link to other cabinets is never up here, so the verdict is cleared while
 * the category the title filled in is kept. */
void __attribute__((stdcall)) wmmt5NetworkState(uint32_t *result)
{
    GuestTls::HostCallScope hostCall;
    void *self = reinterpret_cast<void **>(__builtin_frame_address(0))[3];
    if (!result)
        return;
    GuestTls::EnterGuestCode();
    asm volatile("pushl %1\n\tpushl %0\n\tcall *%2\n\taddl $4, %%esp\n\t"
                 :
                 : "r"(result), "r"(self), "r"(g_originalNetworkState)
                 : "eax", "ecx", "edx", "memory", "cc");
    GuestTls::EnterHostCall();
    result[0] = 0;
}

int wmmt5LinkCheck(void) { return 0; }
int wmmt5PeerCheck(void) { return 0; }

/* Billing has no server to save to, and writing the record is what crashes. */
void wmmt5BillingSave(void) {}

/* A token marked with a leading '@' passes through unencrypted. */
int wmmt5DecryptToken(char *destination, int *destinationSize, char *source, void *context)
{
    if (source && source[0] == '@')
    {
        const size_t length = std::strlen(source + 1);
        std::memcpy(destination, source + 1, length);
        if (destinationSize)
            *destinationSize = static_cast<int>(length);
        return 0;
    }
    if (!g_originalDecryptToken)
        return -1;
    return g_originalDecryptToken(destination, destinationSize, source, context);
}

/* The network monitor's step function. Each step asks the cabinet's monitoring
 * daemon over /Sys.Monitor.* and waits for a reply that never comes, so the
 * monitor keeps its initial "not checked" error and the boot check shows
 * E0001. The verdicts are filled in here the way a healthy LAN would answer. */
constexpr uint8_t MonitorStepSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53,
                                            0x81, 0xec, 0xfc, 0x01, 0x00, 0x00};

/* Offsets into the monitor object; the same in every build. */
constexpr size_t MonitorStepOffset = 0x220;
constexpr size_t CableOffset = 0x1dc;
constexpr size_t GatewayOffset = 0x1e0;
constexpr size_t ShopRouterOffset = 0x178;
constexpr size_t HopsOffset = 0x1f4;
constexpr size_t HopsStateOffset = 0x1ec;
constexpr size_t NtpOffset = 0x1d8;
constexpr size_t OnlineOffset = 0x1fc;
constexpr size_t RenewOffset = 0x204;
constexpr int VerdictGood = 3;

int (*g_originalMonitorStep)(uint8_t *, int) = nullptr;

int wmmt5MonitorStep(uint8_t *monitor, int argument)
{
    GuestTls::HostCallScope hostCall;
    if (monitor)
    {
        auto field = [monitor](size_t offset) -> int & {
            return *reinterpret_cast<int *>(monitor + offset);
        };
        int &step = field(MonitorStepOffset);
        switch (step)
        {
        case 0:
            step = 1;
            break;
        case 1: /* checking the cable */
            step = 2;
            field(CableOffset) = VerdictGood;
            break;
        case 3:
            field(GatewayOffset) = VerdictGood;
            break;
        case 4:
            field(ShopRouterOffset) = VerdictGood;
            break;
        case 5: /* the shop router is one hop away */
            field(HopsOffset) = 1;
            field(HopsStateOffset) = VerdictGood;
            break;
        case 7:
            field(NtpOffset) = VerdictGood;
            break;
        case 14:
            field(OnlineOffset) = 0;
            break;
        case 15:
            field(CableOffset) = 0;
            break;
        case 16:
            field(RenewOffset) = 0;
            break;
        default:
            break;
        }
    }

    GuestTls::EnterGuestCode();
    const int result = g_originalMonitorStep(monitor, argument);
    GuestTls::EnterHostCall();
    return result;
}

/* Refreshes eth0's address, mask, gateway, DNS and MAC. It reads the gateway
 * from a NETLINK_ROUTE socket, which the host cannot open, so it fails with
 * EAFNOSUPPORT after everything else has been filled in; the content router
 * check then reports the failure as E0001 "Unknown error". */
constexpr uint8_t InterfaceUpdateSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53,
                                                0x81, 0xec, 0x4c, 0x24, 0x00, 0x00};

/* Only the fields read here; the name, DNS list and MAC follow. */
struct Wmmt5Interface
{
    int32_t errorValue;
    uint32_t errorCategory;
    uint32_t name;
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
};

bool (*g_originalInterfaceUpdate)(Wmmt5Interface *) = nullptr;

bool wmmt5InterfaceUpdate(Wmmt5Interface *interfaceState)
{
    GuestTls::HostCallScope hostCall;
    GuestTls::EnterGuestCode();
    const bool updated = g_originalInterfaceUpdate(interfaceState);
    GuestTls::EnterHostCall();
    if (updated || !interfaceState || !interfaceState->address)
        return updated;

    /* The router sits at .254 on an ES1 network; the content router hook
     * reports the same address. */
    interfaceState->gateway = (interfaceState->address & interfaceState->netmask) | 254u;
    interfaceState->errorValue = 0;

    static bool reported = false;
    if (!reported)
    {
        reported = true;
        const uint32_t address = interfaceState->address;
        log_info("System ES1 WMMT5 network: eth0 %u.%u.%u.%u, no route table, gateway set to .254",
                 address >> 24, (address >> 16) & 0xff, (address >> 8) & 0xff, address & 0xff);
    }
    return true;
}

/* True once five days have passed since the last Mucha authentication, which
 * stops the cabinet on E0554. A fresh save has never authenticated, so without
 * a server this fires on the first boot; the loader runs the cabinet offline. */
constexpr uint8_t OfflineExpiredSignature[] = {0x55, 0x31, 0xc0, 0x89, 0xe5, 0x53, 0x83,
                                               0xec, 0x14, 0x8b, 0x5d, 0x08, 0x83, 0xbb};

bool wmmt5OfflineExpired(void *) { return false; }

/* Writes over guest code or data after checking what is already there. */
bool patchGuest(uintptr_t address, const void *expected, const void *replacement, size_t length,
                const char *name)
{
    void *target = reinterpret_cast<void *>(address);
    if (std::memcmp(target, expected, length) != 0)
    {
        log_error("WMMT5: %s is not where expected at %p; leaving it alone", name, target);
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &previous))
    {
        log_error("WMMT5: could not unprotect %s at %p", name, target);
        return false;
    }
    std::memcpy(target, replacement, length);
    VirtualProtect(target, length, previous, &previous);
    log_info("System ES1 WMMT5 network: patched %s at %p", name, target);
    return true;
}

} // namespace

void es1Wmmt5InstallNetworkHooks(void)
{
    const Wmmt5Build *build = es1Wmmt5Build();
    if (!build)
        return;

    const Es1HookSpec hooks[] = {
        {build->contentRouter, reinterpret_cast<void *>(wmmt5ContentRouter), "contentRouter",
         nullptr, ContentRouterSignature, sizeof(ContentRouterSignature)},
        {build->networkState, reinterpret_cast<void *>(wmmt5NetworkState), "networkState",
         &g_originalNetworkState, NetworkStateSignature, sizeof(NetworkStateSignature)},
        {build->linkCheck, reinterpret_cast<void *>(wmmt5LinkCheck), "linkCheck", nullptr,
         LinkCheckSignature, sizeof(LinkCheckSignature)},
        {build->peerCheck, reinterpret_cast<void *>(wmmt5PeerCheck), "peerCheck", nullptr,
         PeerCheckSignature, sizeof(PeerCheckSignature)},
        {build->billingSave, reinterpret_cast<void *>(wmmt5BillingSave), "billingSave", nullptr,
         BillingSaveSignature, sizeof(BillingSaveSignature)},
        {build->decryptToken, reinterpret_cast<void *>(wmmt5DecryptToken), "decryptToken",
         reinterpret_cast<void **>(&g_originalDecryptToken), DecryptTokenSignature,
         sizeof(DecryptTokenSignature)},
        {build->monitorStep, reinterpret_cast<void *>(wmmt5MonitorStep), "networkMonitorStep",
         reinterpret_cast<void **>(&g_originalMonitorStep), MonitorStepSignature,
         sizeof(MonitorStepSignature)},
        {build->interfaceUpdate, reinterpret_cast<void *>(wmmt5InterfaceUpdate), "interfaceUpdate",
         reinterpret_cast<void **>(&g_originalInterfaceUpdate), InterfaceUpdateSignature,
         sizeof(InterfaceUpdateSignature)},
        {build->offlineExpired, reinterpret_cast<void *>(wmmt5OfflineExpired), "offlineExpired",
         nullptr, OfflineExpiredSignature, sizeof(OfflineExpiredSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 network");

    patchGuest(build->routerAccept, RouterAcceptExpected, RouterAcceptPatched,
               sizeof(RouterAcceptExpected), "content router accept");
    patchGuest(build->routerReject, RouterRejectExpected, RouterRejectPatched,
               sizeof(RouterRejectExpected), "content router reject");
    if (build->muchaHost)
        patchGuest(build->muchaHost, MuchaHostExpected, MuchaHostLocal, sizeof(MuchaHostLocal),
                   "mucha hostname");
}

#endif
