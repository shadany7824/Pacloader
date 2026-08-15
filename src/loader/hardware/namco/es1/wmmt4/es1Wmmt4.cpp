#include "es1Wmmt4.h"
#include "../es1CompatLayer.h"
#include "../es1Network.h"
#include "es1Wmmt4Card.hpp"
#include "es1Wmmt4Ffb.hpp"
#include "es1Wmmt4Network.hpp"
#include "es1Wmmt4Terminal.hpp"
#include "../../../../elfLoader/guestTls.hpp"

#include "../../../../config/config.h"
#include "../../../../graphics/sdlCalls.h"
#include "../../../../input/sdlInput.h"
#include "../../../../log/log.h"
#include "../../../common/jvs.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include <windows.h>

extern int gWidth;
extern int gHeight;

namespace
{
/* Dongle serials for the two cabinets in a WMMT4 group; they differ in the
 * fifth digit, the same way the ALL.Net serials do. */
constexpr char DriveHaspSerial[] = "267610069420";
constexpr char TerminalHaspSerial[] = "267611069420";

/* Opening bytes at each address, so a build these were not taken from is refused
 * rather than silently hooked; see Es1HookSpec. From WMN4r Rev 1.10.18. */
constexpr uintptr_t JvioMonitorAddress = 0x8390ba0;
constexpr uint8_t JvioMonitorSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x83, 0xec,
                                            0x3c, 0x0f, 0xb6, 0x45, 0x08, 0x8b, 0x55, 0x0c};
constexpr uintptr_t HaspLoginAddress = 0x8921fd0;
constexpr uintptr_t HaspLogoutAddress = 0x89281f4;
constexpr uintptr_t HaspDecryptAddress = 0x8922248;
constexpr uintptr_t HaspGetSizeAddress = 0x8922f60;
constexpr uintptr_t HaspReadAddress = 0x8922dc8;
constexpr uintptr_t HaspWriteAddress = 0x8922e94;
constexpr uintptr_t SendCommandAddress = 0x89e5c80;

/* The title runs its own network diagnostic before online authentication.
 * These entry points adapt the generic ES1 eth0 device to its state layout. */
constexpr uintptr_t SystemLogAddress = 0x809ddb0;
constexpr uintptr_t NetworkAddressChangedAddress = 0x821c920;
constexpr uintptr_t NetworkUpdateStateAddress = 0x80aa7d0;
constexpr uintptr_t NetworkGetInstanceAddress = 0x80a5c60;
constexpr uintptr_t NetworkApplyStateAddress = 0x80a9b70;
constexpr uintptr_t NetworkInterfaceUpdateAddress = 0x821d4f0;
constexpr uintptr_t GuestStringAssignAddress = 0x80579c8;
constexpr uintptr_t LoadRequestAddress = 0x809bc40;
constexpr uintptr_t FileCallbackAddress = 0x809b480;
constexpr uintptr_t NuMemoryAllocAddress = 0x89955c0;
constexpr uintptr_t NuMemoryFreeAddress = 0x8995620;
constexpr uintptr_t HeapAllocAddress = 0x805e4f0;
constexpr uintptr_t HeapReleaseAddress = 0x805e710;

/* Filename wildcard matcher used by the title's directory enumerator. */
constexpr uintptr_t FilenameMatchAddress = 0x89adf50;

/* Sys::Device::TouchIo::update().  The terminal cabinet's boot check fails
 * with E2405 unless the panel reports that it finished booting; the drive
 * cabinet has no touch panel and never reaches this. */
constexpr uintptr_t TouchUpdateAddress = 0x837e4a0;

std::atomic<unsigned int> g_hookTraceMask{0};
std::atomic<int> g_lastNetworkCheck{-1};

using Wmmt4LoadRequestOriginal = int (*)(void *);
using Wmmt4FileCallbackOriginal = void (*)(int, void *, int, void *);
Wmmt4LoadRequestOriginal g_originalLoadRequest = nullptr;
Wmmt4FileCallbackOriginal g_originalFileCallback = nullptr;
using Wmmt4FilenameMatchOriginal = int (*)(const char *, const char *);
Wmmt4FilenameMatchOriginal g_originalFilenameMatch = nullptr;

void traceHook(unsigned int bit, const char *name)
{
    const unsigned int mask = 1u << bit;
    if ((g_hookTraceMask.fetch_or(mask) & mask) == 0)
    {
        log_info("System ES1 WMMT4: %s hook invoked", name);
        std::fflush(stdout);
    }
}

extern "C" char *wmmt4JvioMonitor(int, int, char *buffer, size_t bufferSize)
{
    GuestTls::HostCallScope hostCall;
    traceHook(6, "JVIO monitor");
    if (buffer && bufferSize)
        buffer[0] = '\0';
    return buffer;
}

#pragma pack(push, 1)
struct Wmmt4PointLimitData
{
    uint8_t initialized;
    uint8_t pad;
    uint16_t settlementMonth;
    uint32_t upperToken;
    uint32_t lowerToken;
    uint32_t checksum;
};

struct Wmmt4PointData
{
    uint32_t cost;
    uint32_t limit;
    uint32_t current;
    uint32_t checksum;
};

struct Wmmt4ApplicationData
{
    Wmmt4PointLimitData pointLimit;
    Wmmt4PointData points[2];
};

struct Wmmt4SystemData
{
    char serial[12];
    char otherInfo[48];
    uint16_t reserved;
    uint8_t checksum;
    uint8_t negativeChecksum;
};

struct Wmmt4HaspData
{
    Wmmt4ApplicationData application;
    uint8_t pad[3280];
    Wmmt4SystemData system;
};
#pragma pack(pop)

static_assert(sizeof(Wmmt4HaspData) == 3392, "WMMT4 HASP layout changed");

Wmmt4HaspData g_haspData{};

uint32_t swap32(uint32_t value)
{
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}

void initializeHaspData()
{
    std::memset(&g_haspData, 0, sizeof(g_haspData));
    g_haspData.application.pointLimit.initialized = 1;
    g_haspData.application.pointLimit.upperToken = swap32(5);
    g_haspData.application.points[0].cost = swap32(1);
    g_haspData.application.points[0].limit = swap32(9999);
    g_haspData.application.points[0].current = swap32(1);
    g_haspData.application.points[1] = g_haspData.application.points[0];

    g_haspData.application.pointLimit.checksum = swap32(
        es1CompatCrc32Mpeg(reinterpret_cast<const uint8_t *>(&g_haspData.application.pointLimit), 12));
    for (Wmmt4PointData &point : g_haspData.application.points)
        point.checksum = swap32(es1CompatCrc32Mpeg(reinterpret_cast<const uint8_t *>(&point), 12));

    const bool terminal =
        getConfig()->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL;
    const char *serial = terminal ? TerminalHaspSerial : DriveHaspSerial;
    std::memcpy(g_haspData.system.serial, serial, sizeof(g_haspData.system.serial));
    uint8_t checksum = 0;
    for (int i = 0; i < 62; ++i)
        checksum = static_cast<uint8_t>(checksum + reinterpret_cast<uint8_t *>(&g_haspData.system)[i]);
    g_haspData.system.checksum = checksum;
    g_haspData.system.negativeChecksum = static_cast<uint8_t>(checksum ^ 0xff);
}

extern "C" int wmmt4HaspLogin(int, char *, int *handle)
{
    GuestTls::HostCallScope hostCall;
    traceHook(7, "HASP login");
    if (handle)
        *handle = 1;
    return 0;
}

extern "C" int wmmt4HaspLogout(int)
{
    GuestTls::HostCallScope hostCall;
    traceHook(8, "HASP logout");
    return 0;
}

extern "C" int wmmt4HaspDecrypt(int, uint8_t *, int)
{
    GuestTls::HostCallScope hostCall;
    traceHook(9, "HASP decrypt");
    return 0;
}

extern "C" int wmmt4HaspGetSize(int, int, int *size)
{
    GuestTls::HostCallScope hostCall;
    traceHook(10, "HASP get size");
    if (size)
        *size = static_cast<int>(sizeof(g_haspData));
    return 0;
}

extern "C" int wmmt4HaspRead(int, int, int offset, int length, uint8_t *buffer)
{
    GuestTls::HostCallScope hostCall;
    traceHook(11, "HASP read");
    return es1CompatReadBlob(reinterpret_cast<const uint8_t *>(&g_haspData),
                              sizeof(g_haspData), offset, length, buffer);
}

extern "C" int wmmt4HaspWrite(int, int, int offset, int length, uint8_t *buffer)
{
    GuestTls::HostCallScope hostCall;
    traceHook(12, "HASP write");
    return es1CompatWriteBlob(reinterpret_cast<uint8_t *>(&g_haspData), sizeof(g_haspData),
                               offset, length, buffer);
}

extern "C" int wmmt4SendCommand(int, int, int, int, char *)
{
    GuestTls::HostCallScope hostCall;
    traceHook(13, "send command");
    return 0;
}

enum class Wmmt4NetworkCheck : uint32_t
{
    None = 0,
    Interface = 1,
    Cable = 2,
    Gateway = 3,
    ShopAddress = 4,
    Hops = 5,
    Auth = 6,
    SyncDate = 7,
    Done = 8,
    Renew = 9,
    Test = 10,
    Wifi = 11,
    SetDate = 12,
    Online = 13,
    CableCheck = 14,
    Ntp = 15,
    PackageFile = 16,
};

enum class Wmmt4NetworkState : uint32_t
{
    None = 0,
    Busy = 1,
    Bad = 2,
    Good = 3,
};

#pragma pack(push, 1)
struct Wmmt4NetworkImpl
{
    uint8_t pad0[436];
    Wmmt4NetworkState resolveState;
    uint8_t pad1[104];
    Wmmt4NetworkState renewState;
    Wmmt4NetworkState syncDateState;
    Wmmt4NetworkState cableState;
    Wmmt4NetworkState gatewayState;
    Wmmt4NetworkState contentRouterState;
    Wmmt4NetworkState shopRouterState;
    Wmmt4NetworkState hopsState;
    uint8_t pad2[4];
    uint32_t hops;
    Wmmt4NetworkState wifiRouterState;
    Wmmt4NetworkState onlineState;
    Wmmt4NetworkState setDateState;
    Wmmt4NetworkState ntpState;
    Wmmt4NetworkState packageFileState;
    uint8_t pad3[28];
    Wmmt4NetworkCheck check;
};

struct Wmmt4Network
{
    Wmmt4NetworkImpl *impl;
};

struct Wmmt4NetworkInterface
{
    uint8_t pad0[8];
    uint8_t name[4];
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint8_t pad1[12];
    uint8_t mac[4];
};

struct Wmmt4LoadRequest
{
    uint8_t pad[276];
    void *user;
    void *heap;
    int alignment;
};
#pragma pack(pop)

static_assert(offsetof(Wmmt4NetworkImpl, check) == 628,
              "WMMT4 network state layout changed");
static_assert(offsetof(Wmmt4NetworkInterface, address) == 12,
              "WMMT4 network interface layout changed");
static_assert(offsetof(Wmmt4NetworkInterface, mac) == 36,
              "WMMT4 network interface string layout changed");
static_assert(offsetof(Wmmt4LoadRequest, user) == 276,
              "WMMT4 load request layout changed");

struct Wmmt4LoadUser
{
    void *user;
    void *heap;
    int alignment;
};

extern "C" void wmmt4SystemLog(int type, const char *format, ...)
{
    GuestTls::HostCallScope hostCall;
    char message[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format ? format : "", args);
    va_end(args);

    /* The title's own severity says nothing useful - it logs "Loading... Meter
     * ok" and every model it entries at type 4 - so taking it at face value put
     * thousands of ERROR lines on the console. It is all game chatter;
     * LL_LOG_LEVEL=game brings it back. */
    (void)type;
    log_game("WMMT4: %s", message);
    std::fflush(stdout);
}

/* Only the fields the title's boot check and menu code read. */
struct Wmmt4TouchIo
{
    uint8_t reserved0[16];
    int32_t booted;
    uint8_t reserved1[60];
    int32_t positionX;
    int32_t positionY;
    int32_t touched;
};

static_assert(offsetof(Wmmt4TouchIo, booted) == 16,
              "WMMT4 touch panel state layout changed");
static_assert(offsetof(Wmmt4TouchIo, touched) == 88,
              "WMMT4 touch panel state layout changed");

extern "C" void wmmt4TouchUpdate(Wmmt4TouchIo *touch)
{
    GuestTls::HostCallScope hostCall;
    traceHook(17, "touch panel update");
    if (!touch)
        return;

    touch->booted = 1;
    touch->positionX = 0;
    touch->positionY = 0;
    touch->touched = 0;

    if (!getConfig()->emulateTouchscreen)
        return;

    SDL_Window *window = getSDLWindow();
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
        return;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    if (windowWidth <= 0 || windowHeight <= 0)
        return;

    const int targetWidth = gWidth > 0 ? gWidth : windowWidth;
    const int targetHeight = gHeight > 0 ? gHeight : windowHeight;
    touch->positionX = std::clamp(static_cast<int>(mouseX * targetWidth / windowWidth), 0, targetWidth - 1);
    touch->positionY = std::clamp(static_cast<int>(mouseY * targetHeight / windowHeight), 0, targetHeight - 1);
    touch->touched = (buttons & SDL_BUTTON_LMASK) != 0;
}

extern "C" bool wmmt4NetworkAddressChanged(void *)
{
    GuestTls::HostCallScope hostCall;
    traceHook(14, "network address-change check");
    return false;
}

extern "C" bool wmmt4NetworkInterfaceUpdate(Wmmt4NetworkInterface *interfaceState)
{
    GuestTls::HostCallScope hostCall;
    traceHook(15, "network interface update");
    if (!interfaceState)
        return false;

    /* Report the address the sockets actually use: terminal discovery receives
     * its own datagram back and recognises it only by comparing the source
     * against this, so a made-up address makes the cabinet wait forever. */
    int interfaceIndex = 0;
    unsigned char address[4] = {127, 0, 0, 1};
    unsigned char mask[4] = {255, 255, 255, 0};
    unsigned char mac[6] = {0};
    int link = 1;
    es1HostNetworkInterface(&interfaceIndex, address, mask, mac, &link);

    const auto pack = [](const unsigned char octets[4]) {
        return (static_cast<uint32_t>(octets[0]) << 24) | (static_cast<uint32_t>(octets[1]) << 16) |
               (static_cast<uint32_t>(octets[2]) << 8) | static_cast<uint32_t>(octets[3]);
    };

    /* These fields use host-order integer representations in the title. */
    interfaceState->address = pack(address);
    interfaceState->netmask = pack(mask);
    /* The content router check rejects .1: ES1 installations put it at .254,
     * and reporting anything else fails the boot check with
     * E0502 "Content Router mismatch". */
    interfaceState->gateway = (pack(address) & pack(mask)) | 254u;

    char macText[13] = {0};
    std::snprintf(macText, sizeof(macText), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);

    using GuestStringAssign = void (*)(void *, const char *);
    auto assign = reinterpret_cast<GuestStringAssign>(GuestStringAssignAddress);
    GuestTls::EnterGuestCode();
    assign(interfaceState->name, "eth0");
    assign(interfaceState->mac, macText);
    GuestTls::EnterHostCall();

    log_info("System ES1 WMMT4: eth0 reported as %u.%u.%u.%u/%u.%u.%u.%u mac %s", address[0],
             address[1], address[2], address[3], mask[0], mask[1], mask[2], mask[3], macText);
    return true;
}

extern "C" void wmmt4NetworkUpdateState(void *stateArray)
{
    GuestTls::HostCallScope hostCall;
    traceHook(16, "network diagnostic update");

    using GuestGetNetwork = Wmmt4Network *(*)();
    using GuestApplyState = void (*)(Wmmt4NetworkImpl *, void *);
    auto getNetwork = reinterpret_cast<GuestGetNetwork>(NetworkGetInstanceAddress);
    auto applyState = reinterpret_cast<GuestApplyState>(NetworkApplyStateAddress);

    GuestTls::EnterGuestCode();
    Wmmt4Network *network = getNetwork();
    GuestTls::EnterHostCall();
    if (!network || !network->impl)
        return;

    Wmmt4NetworkImpl *impl = network->impl;
    const int check = static_cast<int>(impl->check);
    if (g_lastNetworkCheck.exchange(check) != check)
    {
        log_info("System ES1 WMMT4: network diagnostic state %d", check);
        std::fflush(stdout);
    }

    /* LL_WMMT4_NET_TRACE=1: every change in the per-check verdicts.  The title
     * shows its online state on the attract screen without logging it, and
     * these fields are what that display is built from. */
    static const bool traceNetwork = std::getenv("LL_WMMT4_NET_TRACE") != nullptr;
    if (traceNetwork)
    {
        struct Watched { const char *name; Wmmt4NetworkState value; };
        const Watched watched[] = {
            {"cable", impl->cableState},       {"gateway", impl->gatewayState},
            {"shopRouter", impl->shopRouterState},
            {"contentRouter", impl->contentRouterState},
            {"hops", impl->hopsState},         {"resolve", impl->resolveState},
            {"online", impl->onlineState},     {"ntp", impl->ntpState},
            {"setDate", impl->setDateState},   {"syncDate", impl->syncDateState},
            {"renew", impl->renewState},       {"wifi", impl->wifiRouterState},
            {"packageFile", impl->packageFileState},
        };
        static uint32_t previous[sizeof(watched) / sizeof(watched[0])] = {};
        static bool primed = false;
        for (size_t i = 0; i < sizeof(watched) / sizeof(watched[0]); ++i)
        {
            const uint32_t now = static_cast<uint32_t>(watched[i].value);
            if (primed && now != previous[i])
                log_info("LLNET %s: %u -> %u (check=%d)", watched[i].name, previous[i], now,
                         check);
            previous[i] = now;
        }
        primed = true;
    }

    /* Once verified the cable stays verified: the title re-checks about once a
     * second and drops the state to Busy meanwhile, and a virtual LAN cannot
     * come unplugged.  Only the re-checks are pinned. */
    static std::atomic<bool> cableVerified{false};
    if (impl->cableState == Wmmt4NetworkState::Good)
        cableVerified.store(true, std::memory_order_relaxed);
    else if (cableVerified.load(std::memory_order_relaxed))
        impl->cableState = Wmmt4NetworkState::Good;

    /* A standalone cabinet still has a valid local link.  Advance all local
     * hardware checks and leave Auth untouched, so the title itself produces
     * its normal offline/service-unavailable result. */
    switch (impl->check)
    {
    case Wmmt4NetworkCheck::Cable:
    case Wmmt4NetworkCheck::CableCheck:
        impl->cableState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Gateway:
        impl->gatewayState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::ShopAddress:
        impl->resolveState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Hops:
        impl->hops = 1;
        impl->hopsState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::SyncDate:
        impl->syncDateState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Renew:
        impl->renewState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Test:
        impl->cableState = Wmmt4NetworkState::Good;
        impl->shopRouterState = Wmmt4NetworkState::Good;
        impl->contentRouterState = Wmmt4NetworkState::Good;
        impl->hopsState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Wifi:
        impl->wifiRouterState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::SetDate:
        impl->setDateState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Online:
        impl->onlineState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::Ntp:
        impl->ntpState = Wmmt4NetworkState::Good;
        break;
    case Wmmt4NetworkCheck::PackageFile:
        impl->packageFileState = Wmmt4NetworkState::Good;
        break;
    default:
        break;
    }

    /* What the title actually publishes, after the fixes above.  The trace at
     * the top of this hook shows the value the game wrote; this one shows what
     * the display gets, so the two together say whether a fix took effect. */
    if (traceNetwork)
    {
        static uint32_t lastPublishedCable = 0xffffffffu;
        const uint32_t published = static_cast<uint32_t>(impl->cableState);
        if (published != lastPublishedCable)
        {
            log_info("LLNET published cable=%u (check=%d)", published, check);
            lastPublishedCable = published;
        }
    }

    GuestTls::EnterGuestCode();
    applyState(impl, stateArray);
    GuestTls::EnterHostCall();
}

extern "C" int wmmt4LoadRequest(Wmmt4LoadRequest *request)
{
    GuestTls::HostCallScope hostCall;
    traceHook(17, "resource load request");
    if (!request || !g_originalLoadRequest)
        return -1;

    auto *wrapper = new Wmmt4LoadUser{request->user, request->heap, request->alignment};
    request->user = wrapper;
    GuestTls::EnterGuestCode();
    const int result = g_originalLoadRequest(request);
    GuestTls::EnterHostCall();
    return result;
}

extern "C" void wmmt4FileCallback(int result, void *buffer, int size, void *user)
{
    GuestTls::HostCallScope hostCall;
    traceHook(18, "resource file callback");
    auto *wrapper = static_cast<Wmmt4LoadUser *>(user);
    if (!wrapper || !g_originalFileCallback)
        return;

    auto callOriginal = [&](int callbackResult, void *callbackBuffer, int callbackSize) {
        GuestTls::EnterGuestCode();
        g_originalFileCallback(callbackResult, callbackBuffer, callbackSize, wrapper->user);
        GuestTls::EnterHostCall();
    };

    std::vector<uint8_t> decompressed;
    if (result == 0 && size > 1 && buffer &&
        es1CompatGzipDecompress(static_cast<const uint8_t *>(buffer),
                                static_cast<size_t>(size), decompressed))
    {
        const size_t alignment = wrapper->alignment > 0
                                     ? static_cast<size_t>(wrapper->alignment)
                                     : 1u;
        const size_t alignedSize = (decompressed.size() + alignment - 1) /
                                   alignment * alignment;
        using NuMemoryAlloc = void *(*)(int, size_t, size_t);
        using NuMemoryFree = void (*)(void *);
        using HeapAlloc = void *(*)(size_t, size_t, void *);
        using HeapRelease = void (*)(void *, void *);
        void *newBuffer = nullptr;

        GuestTls::EnterGuestCode();
        if (wrapper->heap)
        {
            reinterpret_cast<HeapRelease>(HeapReleaseAddress)(buffer, wrapper->heap);
            newBuffer = reinterpret_cast<HeapAlloc>(HeapAllocAddress)(
                alignedSize, alignment, wrapper->heap);
        }
        else
        {
            reinterpret_cast<NuMemoryFree>(NuMemoryFreeAddress)(buffer);
            newBuffer = reinterpret_cast<NuMemoryAlloc>(NuMemoryAllocAddress)(
                0, alignment, alignedSize);
        }
        GuestTls::EnterHostCall();

        if (newBuffer)
        {
            std::memcpy(newBuffer, decompressed.data(), decompressed.size());
            log_debug("System ES1 WMMT4: decompressed resource %d -> %u bytes",
                      size, static_cast<unsigned>(decompressed.size()));
            callOriginal(0, newBuffer, static_cast<int>(decompressed.size()));
            delete wrapper;
            return;
        }
        log_error("System ES1 WMMT4: failed to allocate %u-byte decompressed resource",
                  static_cast<unsigned>(alignedSize));
        callOriginal(-1, nullptr, 0);
        delete wrapper;
        return;
    }

    callOriginal(result, buffer, size);
    delete wrapper;
}

extern "C" int wmmt4FilenameMatch(const char *name, const char *pattern)
{
    GuestTls::HostCallScope hostCall;

    /*
     * The title's enumerator has no end-of-directory branch and would compute
     * NULL->d_name here; bridgeReaddir() reports EBADF so it never gets that
     * far, and this guard stays as an anomaly detector.
     */
    if (reinterpret_cast<uintptr_t>(name) < 0x10000u ||
        reinterpret_cast<uintptr_t>(pattern) < 0x10000u)
    {
        static std::atomic<unsigned int> invalidCount{0};
        const unsigned int count = invalidCount.fetch_add(1) + 1;
        if (count <= 4)
            log_warn("System ES1 WMMT4: ignored invalid directory entry name=%p pattern=%p \"%s\"",
                     name, pattern,
                     reinterpret_cast<uintptr_t>(pattern) >= 0x10000u ? pattern : "<bad>");
        return 0;
    }

    if (!g_originalFilenameMatch)
        return 0;

    GuestTls::EnterGuestCode();
    const int result = g_originalFilenameMatch(name, pattern);
    GuestTls::EnterHostCall();
    return result;
}
}

extern "C" int es1Wmmt4Detect(const char *elfPath)
{
    const std::filesystem::path elf(elfPath);
    const std::filesystem::path gameDir = elf.parent_path();
    /* Pacloader supplies the complete WMMT4 HASP image internally, so a clean
     * game folder needs no mt4hasp.so patch or wm4_hasp.bin marker to be
     * recognised. */
    return elf.filename() == "WMN4r" &&
           std::filesystem::exists(gameDir / "wangan4_exec") &&
           std::filesystem::exists(gameDir / "wangan4_storage") &&
           std::filesystem::exists(gameDir / "data") &&
           (std::filesystem::exists(gameDir / "ll-deps") ||
            std::filesystem::exists(gameDir / "libso"))
               ? 1
               : 0;
}

extern "C" int es1Wmmt4InstallHooks(void)
{
    initializeHaspData();
    const bool terminal =
        getConfig()->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL;
    log_info("System ES1 WMMT4: cabinet mode %s, virtual HASP S/N %.12s",
             terminal ? "TERMINAL" : "DRIVE", g_haspData.system.serial);
    if (!terminal || std::getenv("LL_WMMT4_FORCE_TERMINAL_EMULATOR"))
        wmmt4StartTerminalEmulator(g_haspData.system.serial);
    else if (getConfig()->namcoES1.terminalEmulatorEnabled)
        log_warn("System ES1 WMMT4: terminal emulator disabled in TERMINAL cabinet mode");

    const Es1HookSpec hooks[] = {
        /* The JVIO subsystem is left unhooked: the title runs a real JVS master
         * the virtual board answers. Replacing JvioControl_Update also skipped the
         * output acknowledgement, reported as "Gout Update Timeout". */
        {JvioMonitorAddress, reinterpret_cast<void *>(wmmt4JvioMonitor), "JvioMonitor",
         nullptr, JvioMonitorSignature, sizeof(JvioMonitorSignature)},
        {HaspLoginAddress, reinterpret_cast<void *>(wmmt4HaspLogin), "hasp_login"},
        {HaspLogoutAddress, reinterpret_cast<void *>(wmmt4HaspLogout), "hasp_logout"},
        {HaspDecryptAddress, reinterpret_cast<void *>(wmmt4HaspDecrypt), "hasp_decrypt"},
        {HaspGetSizeAddress, reinterpret_cast<void *>(wmmt4HaspGetSize), "hasp_get_size"},
        {HaspReadAddress, reinterpret_cast<void *>(wmmt4HaspRead), "hasp_read"},
        {HaspWriteAddress, reinterpret_cast<void *>(wmmt4HaspWrite), "hasp_write"},
        {SendCommandAddress, reinterpret_cast<void *>(wmmt4SendCommand), "send_command"},
        {SystemLogAddress, reinterpret_cast<void *>(wmmt4SystemLog), "Sys_LogMes"},
        {TouchUpdateAddress, reinterpret_cast<void *>(wmmt4TouchUpdate), "Sys_Device_TouchIo_update"},
        {NetworkAddressChangedAddress, reinterpret_cast<void *>(wmmt4NetworkAddressChanged),
         "Sys_Net_interface_isAddressChange"},
        {NetworkInterfaceUpdateAddress, reinterpret_cast<void *>(wmmt4NetworkInterfaceUpdate),
         "Sys_Net_Interface_update"},
        {LoadRequestAddress, reinterpret_cast<void *>(wmmt4LoadRequest),
         "Sys_LoadRequest_stdLoad", reinterpret_cast<void **>(&g_originalLoadRequest)},
        {FileCallbackAddress, reinterpret_cast<void *>(wmmt4FileCallback),
         "Sys_fileDescCallback", reinterpret_cast<void **>(&g_originalFileCallback)},
        {FilenameMatchAddress, reinterpret_cast<void *>(wmmt4FilenameMatch),
         "directory_filename_match", reinterpret_cast<void **>(&g_originalFilenameMatch)},
    };
    int installed = es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
    if (!std::getenv("LL_WMMT4_NATIVE_NETWORK_STATE"))
    {
        const Es1HookSpec networkStateHook[] = {
            {NetworkUpdateStateAddress, reinterpret_cast<void *>(wmmt4NetworkUpdateState),
             "Sys_Network_UpdateState"},
        };
        installed += es1InstallHookTable(networkStateHook, 1, "WMMT4 network state");
    }
    else
    {
        log_warn("System ES1 WMMT4: using native network state updates");
    }
    installed += wmmt4InstallCardHooks() + wmmt4InstallFfbHooks();
    wmmt4InstallNetworkDiagnostics();

    log_info("System ES1 WMMT4: installed %d version-specific compatibility hooks", installed);
    return installed >= 2 ? 0 : -1;
}
