#include "es1Wmmt4Terminal.hpp"
#include "../es1Network.h"

#include "../../../../config/config.h"
#include "../../../../log/log.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace
{
/* Send rate.  The cabinet empties the terminal's peer-table entry every frame
 * and refills it from that frame's datagram, so anything slower than the 16.6 ms
 * frame leaves the attract banner reading "cards cannot be used" for a moment. */
constexpr unsigned int TerminalIntervalMilliseconds = 4;

/* Multicast group the cabinet LAN uses, and the emulator's source port. */
constexpr char TerminalGroupAddress[] = "225.0.0.1";
constexpr unsigned short TerminalGroupPort = 50765;
constexpr unsigned short TerminalSourcePort = 50765;

std::atomic<bool> g_started{false};
const char *g_heartbeatSerial = nullptr;

bool terminalEmulatorEnabled()
{
    const NamcoES1Config &config = getConfig()->namcoES1;
    return config.terminalEmulatorEnabled != 0;
}

using TerminalPacket = std::vector<uint8_t>;

void appendProtoVarint(TerminalPacket &output, uint64_t value)
{
    do
    {
        uint8_t byte = static_cast<uint8_t>(value & 0x7fu);
        value >>= 7;
        if (value)
            byte |= 0x80u;
        output.push_back(byte);
    } while (value);
}

void appendProtoInt32(TerminalPacket &output, unsigned field, int32_t value)
{
    appendProtoVarint(output, static_cast<uint64_t>(field) << 3);
    appendProtoVarint(output, static_cast<uint64_t>(static_cast<int64_t>(value)));
}

void appendProtoUInt32(TerminalPacket &output, unsigned field, uint32_t value)
{
    appendProtoVarint(output, static_cast<uint64_t>(field) << 3);
    appendProtoVarint(output, value);
}

void appendProtoString(TerminalPacket &output, unsigned field, const char *value)
{
    const size_t length = std::strlen(value);
    appendProtoVarint(output, (static_cast<uint64_t>(field) << 3) | 2u);
    appendProtoVarint(output, length);
    output.insert(output.end(), value, value + length);
}

void appendProtoMessage(TerminalPacket &output, unsigned field,
                        const TerminalPacket &message)
{
    appendProtoVarint(output, (static_cast<uint64_t>(field) << 3) | 2u);
    appendProtoVarint(output, message.size());
    output.insert(output.end(), message.begin(), message.end());
}

TerminalPacket makeTerminalCar()
{
    TerminalPacket car;
    appendProtoUInt32(car, 1, 0);  // car_id
    appendProtoUInt32(car, 2, 0);  // region_id
    appendProtoString(car, 3, "");
    appendProtoString(car, 4, "");
    appendProtoUInt32(car, 5, 0);  // manufacturer
    appendProtoUInt32(car, 7, 0);  // visual_model
    appendProtoUInt32(car, 8, 0);  // default_color
    for (unsigned field = 9; field <= 26; ++field)
        appendProtoUInt32(car, field, 0);
    appendProtoUInt32(car, 27, 47); // level
    return car;
}

TerminalPacket makeTerminalBatchSettings(uint32_t now)
{
    TerminalPacket settings;
    const int32_t values[] = {
        1,     // coin_chute
        3,     // buycard_cost
        1,     // game_cost
        1,     // continue_cost
        1,     // fullcourse_cost
        1,     // freeplay
        1,     // wins_and_remains
        0,     // event_mode
        0,     // event_mode_dist
        2,     // close_type
        96, 96, 96, 96, 96, 96, 96, 96,
        11018, // software_revision
    };
    for (unsigned field = 1; field <= sizeof(values) / sizeof(values[0]); ++field)
        appendProtoInt32(settings, field, values[field - 1]);
    appendProtoInt32(settings, 20, static_cast<int32_t>(now));
    return settings;
}

TerminalPacket makeTerminalMessage(uint32_t frameNumber, uint32_t now)
{
    TerminalPacket message;

    appendProtoMessage(message, 2, makeTerminalCar());

    TerminalPacket course;
    appendProtoInt32(course, 6, -1); // player_no
    appendProtoInt32(course, 7, 0);  // player_max
    appendProtoMessage(message, 3, course);

    TerminalPacket heartbeat;
    appendProtoUInt32(heartbeat, 1, frameNumber);
    /* The caller passes the drive's serial: the terminal's makes the cabinet
     * take its "use terminal" path and read the game server out of
     * game_server_options, which is not understood yet. */
    const char *heartbeatSerial = g_heartbeatSerial ? g_heartbeatSerial : "";
    if (const char *override = std::getenv("LL_WMMT4_TERMINAL_SERIAL"))
        if (*override)
            heartbeatSerial = override;
    appendProtoString(heartbeat, 2, heartbeatSerial);
    appendProtoInt32(heartbeat, 3, 1);
    appendProtoMessage(message, 5, heartbeat);

    appendProtoInt32(message, 6, 0); // batch_setting_state
    appendProtoMessage(message, 8, makeTerminalBatchSettings(now));

    TerminalPacket information;
    appendProtoInt32(information, 1, 14);
    appendProtoInt32(information, 2, -1);
    appendProtoInt32(information, 3, 0);
    appendProtoInt32(information, 4, 0);
    appendProtoInt32(information, 5, 0);
    appendProtoInt32(information, 6, 0);
    appendProtoString(information, 7, "");
    for (unsigned field = 8; field <= 14; ++field)
        appendProtoInt32(information, field, 0);
    appendProtoMessage(message, 9, information);
    return message;
}

uint32_t terminalPacketCrc(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < length; ++index)
    {
        crc ^= static_cast<uint32_t>(data[index]) << 24;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

TerminalPacket makeTerminalPacket(uint32_t frameNumber)
{
    const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    const TerminalPacket message = makeTerminalMessage(frameNumber, now);
    TerminalPacket packet;
    packet.reserve(message.size() + 8);
    packet.push_back(1);
    packet.push_back(4); // terminal PCB id
    packet.push_back(0);
    packet.push_back(0);
    packet.insert(packet.end(), message.begin(), message.end());

    const uint16_t length = static_cast<uint16_t>(packet.size());
    packet[2] = static_cast<uint8_t>(length & 0xffu);
    packet[3] = static_cast<uint8_t>(length >> 8);
    const uint32_t crc = terminalPacketCrc(packet.data(), packet.size());
    packet.push_back(static_cast<uint8_t>(crc >> 24));
    packet.push_back(static_cast<uint8_t>(crc >> 16));
    packet.push_back(static_cast<uint8_t>(crc >> 8));
    packet.push_back(static_cast<uint8_t>(crc));
    return packet;
}

DWORD WINAPI terminalEmulatorThread(void *)
{
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
    {
        log_error("System ES1 WMMT4 terminal emulator: WSAStartup failed");
        return 0;
    }

    SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET)
    {
        log_error("System ES1 WMMT4 terminal emulator: socket failed (%d)", WSAGetLastError());
        WSACleanup();
        return 0;
    }

    BOOL enabled = TRUE;
    setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&enabled), sizeof(enabled));
    /* Source port.  A real terminal binds the group port, but the cabinet
     * derives the game server's base URL from what it hears, so that choice puts
     * the RPCs on the wrong port.  LL_WMMT4_TERMINAL_SRCPORT overrides it. */
    unsigned short sourcePort = TerminalSourcePort;
    if (const char *override = std::getenv("LL_WMMT4_TERMINAL_SRCPORT"))
    {
        const int wanted = std::atoi(override);
        if (wanted >= 0 && wanted <= 65535)
            sourcePort = static_cast<unsigned short>(wanted);
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(sourcePort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socketHandle, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) ==
        SOCKET_ERROR)
    {
        log_error("System ES1 WMMT4 terminal emulator: bind UDP %u failed (%d)", sourcePort,
                  WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 0;
    }
    {
        sockaddr_in bound{};
        int boundLength = sizeof(bound);
        if (getsockname(socketHandle, reinterpret_cast<sockaddr *>(&bound), &boundLength) == 0)
            log_info("System ES1 WMMT4 terminal emulator: source port %u",
                     ntohs(bound.sin_port));
    }

    int ttl = 255;
    setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_TTL,
               reinterpret_cast<const char *>(&ttl), sizeof(ttl));
    setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_LOOP,
               reinterpret_cast<const char *>(&enabled), sizeof(enabled));

    /* Pin the outgoing interface: a host with VPN or VM adapters may have no
     * route for the group otherwise.  It has to be the host adapter address,
     * not the virtual eth0 reported to the guest, which nothing here can bind. */
    unsigned char hostAddress[4] = {127, 0, 0, 1};
    es1HostAdapterAddress(hostAddress);
    in_addr multicastInterface{};
    multicastInterface.s_addr =
        htonl((static_cast<uint32_t>(hostAddress[0]) << 24) |
              (static_cast<uint32_t>(hostAddress[1]) << 16) |
              (static_cast<uint32_t>(hostAddress[2]) << 8) |
              static_cast<uint32_t>(hostAddress[3]));
    if (setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<const char *>(&multicastInterface),
                   sizeof(multicastInterface)) == SOCKET_ERROR)
        log_warn("System ES1 WMMT4 terminal emulator: could not pin the multicast "
                 "interface to %u.%u.%u.%u (%d)", hostAddress[0], hostAddress[1],
                 hostAddress[2], hostAddress[3], WSAGetLastError());
    else
        log_info("System ES1 WMMT4 terminal emulator: multicasting from %u.%u.%u.%u",
                 hostAddress[0], hostAddress[1], hostAddress[2], hostAddress[3]);

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(TerminalGroupPort);
    inet_pton(AF_INET, TerminalGroupAddress, &destination.sin_addr);
    log_info("System ES1 WMMT4 terminal emulator: broadcasting on %s:%u", TerminalGroupAddress,
             TerminalGroupPort);

    unsigned int intervalMilliseconds = TerminalIntervalMilliseconds;
    if (const char *override = std::getenv("LL_WMMT4_TERMINAL_INTERVAL_MS"))
    {
        const int wanted = std::atoi(override);
        if (wanted > 0 && wanted <= 5000)
            intervalMilliseconds = static_cast<unsigned int>(wanted);
    }
    /* Sleep() rounds up to the system timer tick (15.6 ms by default), which
     * would turn any interval below that into 16 ms. */
    HANDLE sendTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                              CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_ALL_ACCESS);
    if (sendTimer)
    {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(intervalMilliseconds) * 10000;
        if (!SetWaitableTimer(sendTimer, &due, static_cast<LONG>(intervalMilliseconds), nullptr,
                              nullptr, FALSE))
        {
            CloseHandle(sendTimer);
            sendTimer = nullptr;
        }
    }
    log_info("System ES1 WMMT4 terminal emulator: sending every %u ms (%s)",
             intervalMilliseconds,
             sendTimer ? "high-resolution timer" : "Sleep, expect timer-tick jitter");

    uint32_t frameNumber = 0;
    unsigned int consecutiveFailures = 0;
    for (;;)
    {
        const TerminalPacket packet = makeTerminalPacket(++frameNumber);
        if (sendto(socketHandle, reinterpret_cast<const char *>(packet.data()),
                   static_cast<int>(packet.size()), 0,
                   reinterpret_cast<const sockaddr *>(&destination),
                   sizeof(destination)) == SOCKET_ERROR)
        {
            /* Keep going: one transient error would otherwise remove the
             * terminal for the rest of the run.  Report the first few and then
             * stay quiet so a persistent failure cannot flood the log. */
            ++consecutiveFailures;
            if (consecutiveFailures <= 3)
                log_error("System ES1 WMMT4 terminal emulator: send failed (%d)%s",
                          WSAGetLastError(),
                          consecutiveFailures == 3 ? "; further failures suppressed" : "");
        }
        else if (consecutiveFailures != 0)
        {
            log_info("System ES1 WMMT4 terminal emulator: sending again after %u failures",
                     consecutiveFailures);
            consecutiveFailures = 0;
        }
        if (sendTimer)
            WaitForSingleObject(sendTimer, intervalMilliseconds * 2 + 16);
        else
            Sleep(intervalMilliseconds);
    }

    if (sendTimer)
        CloseHandle(sendTimer);
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}
} // namespace

extern "C" void wmmt4StartTerminalEmulator(const char *heartbeatSerial)
{
    if (!terminalEmulatorEnabled() || g_started.exchange(true, std::memory_order_acq_rel))
        return;

    g_heartbeatSerial = heartbeatSerial;
    log_info("System ES1 WMMT4: integrated terminal emulator enabled");

    HANDLE thread = CreateThread(nullptr, 0, terminalEmulatorThread, nullptr, 0, nullptr);
    if (!thread)
    {
        g_started.store(false, std::memory_order_release);
        log_error("System ES1 WMMT4 terminal emulator: thread creation failed (%lu)",
                  static_cast<unsigned long>(GetLastError()));
        return;
    }
    CloseHandle(thread);
}
