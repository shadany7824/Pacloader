#if defined(_WIN32) || defined(__MINGW32__)

#include "filesystemBridge.hpp"
#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "virtualDeviceRegistry.hpp"
#include "guestTls.hpp"
#include "../config/config.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/wmmt4/es1Wmmt4Network.hpp"
#include "../log/log.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <chrono>
#include <vector>
#include <thread>
#include <unordered_map>
#include <windows.h>

extern std::string g_absoluteElfPath;

namespace
{
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

float bridgeStrtofInternal(const char *value, char **end, int group)
{
    (void)group;
    return std::strtof(value, end);
}

long double bridgeStrtoldInternal(const char *value, char **end, int group)
{
    (void)group;
    return std::strtold(value, end);
}

int bridgeAbs(int value)
{
    return std::abs(value);
}

#pragma pack(push, 1)
struct LinuxEpollEvent
{
    uint32_t events;
    uint8_t data[8];
};
#pragma pack(pop)
static_assert(sizeof(LinuxEpollEvent) == 12, "i386 epoll_event layout mismatch");

struct EpollEntry
{
    int fd;
    uint32_t events;
    uint8_t data[8];
    uint32_t lastReadyEvents;
    /* Bytes queued the last time this descriptor was examined; growth is how
     * the readable edge is detected (see filterEdgeTriggeredEvents). */
    long lastReadableBytes;
};

struct EpollInstance
{
    int fd = -1;
    bool used = false;
    std::vector<EpollEntry> entries;
};

struct EventfdInstance
{
    int fd = -1;
    bool used = false;
    bool semaphore = false;
    uint64_t counter = 0;
    bool timerfd = false;
    int timerClockId = 1;
    bool timerArmed = false;
    uint64_t timerNextNanoseconds = 0;
    uint64_t timerIntervalNanoseconds = 0;
};

#pragma pack(push, 4)
struct LinuxTimespec
{
    int32_t seconds;
    int32_t nanoseconds;
};

struct LinuxItimerspec
{
    LinuxTimespec interval;
    LinuxTimespec value;
};
#pragma pack(pop)

static_assert(sizeof(LinuxItimerspec) == 16, "i386 Linux itimerspec layout mismatch");

std::array<EpollInstance, 8> g_epollInstances{};
std::array<EventfdInstance, 8> g_eventfdInstances{};
HostMutex g_epollMutex;
constexpr int FirstEpollDescriptor = 0x4f00;
constexpr int FirstEventfdDescriptor = 0x4e00;
std::atomic<int> g_epollTraceCount{0};

using CurlGlobalInit = int (*)(long);
using CurlGlobalCleanup = void (*)();
using CurlEasyInit = void *(*)();
using CurlEasyCleanup = void (*)(void *);
using CurlEasySetopt = int (*)(void *, int, ...);
using CurlEasyPerform = int (*)(void *);
using CurlEasyGetinfo = int (*)(void *, int, ...);
using CurlSlistAppend = void *(*)(void *, const char *);

struct CurlApi
{
    HMODULE module = nullptr;
    CurlGlobalInit globalInit = nullptr;
    CurlGlobalCleanup globalCleanup = nullptr;
    CurlEasyInit easyInit = nullptr;
    CurlEasyCleanup easyCleanup = nullptr;
    CurlEasySetopt easySetopt = nullptr;
    CurlEasyPerform easyPerform = nullptr;
    CurlEasyGetinfo easyGetinfo = nullptr;
    CurlSlistAppend slistAppend = nullptr;
};

CurlApi g_curlApi;
std::once_flag g_curlLoadOnce;

template <typename Function>
Function curlFunction(HMODULE module, const char *name)
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

bool loadCurlApi()
{
    std::call_once(g_curlLoadOnce, [] {
        for (const char *name : {"libcurl-4.dll", "libcurl.dll"})
        {
            g_curlApi.module = LoadLibraryA(name);
            if (g_curlApi.module)
                break;
        }
        if (!g_curlApi.module)
        {
            log_error("ES1 curl: could not load libcurl-4.dll or libcurl.dll (error %lu)",
                      static_cast<unsigned long>(GetLastError()));
            return;
        }

        g_curlApi.globalInit = curlFunction<CurlGlobalInit>(g_curlApi.module, "curl_global_init");
        g_curlApi.globalCleanup = curlFunction<CurlGlobalCleanup>(g_curlApi.module, "curl_global_cleanup");
        g_curlApi.easyInit = curlFunction<CurlEasyInit>(g_curlApi.module, "curl_easy_init");
        g_curlApi.easyCleanup = curlFunction<CurlEasyCleanup>(g_curlApi.module, "curl_easy_cleanup");
        g_curlApi.easySetopt = curlFunction<CurlEasySetopt>(g_curlApi.module, "curl_easy_setopt");
        g_curlApi.easyPerform = curlFunction<CurlEasyPerform>(g_curlApi.module, "curl_easy_perform");
        g_curlApi.easyGetinfo = curlFunction<CurlEasyGetinfo>(g_curlApi.module, "curl_easy_getinfo");
        g_curlApi.slistAppend = curlFunction<CurlSlistAppend>(g_curlApi.module, "curl_slist_append");
        if (!g_curlApi.globalInit || !g_curlApi.globalCleanup || !g_curlApi.easyInit ||
            !g_curlApi.easyCleanup || !g_curlApi.easySetopt || !g_curlApi.easyPerform ||
            !g_curlApi.easyGetinfo)
        {
            log_error("ES1 curl: loaded libcurl is missing a required easy API symbol");
            FreeLibrary(g_curlApi.module);
            g_curlApi = {};
            return;
        }
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(g_curlApi.module, modulePath, sizeof(modulePath));
        log_info("ES1 curl: using native libcurl DLL %s", modulePath);
    });
    return g_curlApi.module != nullptr;
}

int bridgeCurlGlobalInit(long flags)
{
    const int result = loadCurlApi() ? g_curlApi.globalInit(flags) : 2; /* CURLE_FAILED_INIT */
    log_info("ES1 curl: curl_global_init(%ld) -> %d", flags, result);
    return result;
}

void bridgeCurlGlobalCleanup()
{
    if (loadCurlApi())
    {
        log_info("ES1 curl: curl_global_cleanup()");
        g_curlApi.globalCleanup();
    }
}

void *bridgeCurlEasyInit()
{
    void *handle = loadCurlApi() ? g_curlApi.easyInit() : nullptr;
    log_info("ES1 curl: curl_easy_init() -> %p", handle);
    /* WMMT4's CA bundle, address pins and TLS security level all live in
     * hardware/namco/es1/wmmt4/; this bridge only owns the DLL. */
    wmmt4ConfigureCurlHandle(handle, g_curlApi.easySetopt, g_curlApi.slistAppend);
    return handle;
}

void bridgeCurlEasyCleanup(void *handle)
{
    if (handle && loadCurlApi())
    {
        log_info("ES1 curl: curl_easy_cleanup(%p)", handle);
        g_curlApi.easyCleanup(handle);
    }
}

int bridgeCurlEasySetopt(void *handle, int option, ...)
{
    if (!handle || !loadCurlApi())
        return 43; /* CURLE_BAD_FUNCTION_ARGUMENT */

    va_list arguments;
    va_start(arguments, option);
    int result = 0;
    /* libcurl encodes the vararg type in the option number, so forward each
     * category with its native ABI type rather than discarding the option. */
    if (option >= 30000 && option < 40000)
    {
        const long long value = va_arg(arguments, long long);
        result = g_curlApi.easySetopt(handle, option, value);
        log_info("ES1 curl: curl_easy_setopt(%p, %d, %lld) -> %d", handle, option, value, result);
    }
    else if (option >= 10000)
    {
        void *value = va_arg(arguments, void *);
        result = g_curlApi.easySetopt(handle, option, value);
        /* Only CURLOPT_URL is safe to print as text; the rest are buffers,
         * callbacks and lists. */
        if (option == 10002)
            log_info("ES1 curl: curl_easy_setopt(%p, %d, \"%s\") -> %d", handle, option,
                     value ? static_cast<const char *>(value) : "(null)", result);
        else
            log_info("ES1 curl: curl_easy_setopt(%p, %d, %p) -> %d", handle, option, value, result);
    }
    else
    {
        const long value = va_arg(arguments, long);
        result = g_curlApi.easySetopt(handle, option, value);
        log_info("ES1 curl: curl_easy_setopt(%p, %d, %ld) -> %d", handle, option, value, result);
    }
    va_end(arguments);
    return result;
}

int bridgeCurlEasyPerform(void *handle)
{
    const int result = handle && loadCurlApi() ? g_curlApi.easyPerform(handle) : 43;
    log_info("ES1 curl: curl_easy_perform(%p) -> %d", handle, result);
    return result;
}

int bridgeCurlEasyGetinfo(void *handle, int info, ...)
{
    if (!handle || !loadCurlApi())
        return 43;

    va_list arguments;
    va_start(arguments, info);
    void *value = va_arg(arguments, void *);
    va_end(arguments);
    const int type = info & 0xf00000;
    int result = 0;
    if (type == 0x200000)
        result = g_curlApi.easyGetinfo(handle, info, static_cast<long *>(value));
    else if (type == 0x300000)
        result = g_curlApi.easyGetinfo(handle, info, static_cast<double *>(value));
    else if (type == 0x600000)
        result = g_curlApi.easyGetinfo(handle, info, static_cast<long long *>(value));
    else
        result = g_curlApi.easyGetinfo(handle, info, value);
    log_info("ES1 curl: curl_easy_getinfo(%p, %d) -> %d", handle, info, result);
    return result;
}

uint32_t epollEventsFromWinsock(short events)
{
    uint32_t translated = 0;
    if (events & (POLLRDNORM | POLLIN)) translated |= 0x001; // EPOLLIN
    if (events & (POLLPRI | POLLRDBAND)) translated |= 0x002; // EPOLLPRI
    if (events & (POLLWRNORM | POLLOUT)) translated |= 0x004; // EPOLLOUT
    if (events & POLLERR) translated |= 0x008; // EPOLLERR
    if (events & POLLHUP) translated |= 0x010; // EPOLLHUP
    if (events & POLLNVAL) translated |= 0x020; // EPOLLNVAL
    return translated;
}

short winsockEventsFromEpoll(uint32_t events)
{
    short translated = 0;
    if (events & 0x001) translated |= POLLRDNORM; // EPOLLIN
    if (events & 0x002) translated |= POLLRDBAND; // EPOLLPRI
    if (events & 0x004) translated |= POLLWRNORM; // EPOLLOUT
    return translated;
}

EpollInstance *findEpoll(int fd)
{
    for (EpollInstance &instance : g_epollInstances)
    {
        if (instance.used && instance.fd == fd)
            return &instance;
    }
    return nullptr;
}

/* LL_EPOLL_TRACE=1 dumps what the reactor is actually being told: which
 * descriptors are in the set, what Winsock reported, and what survived the
 * edge filter.  Reading that beats guessing at Asio's state machine. */
bool epollTraceEnabled()
{
    static const bool enabled = std::getenv("LL_EPOLL_TRACE") != nullptr;
    return enabled;
}

uint32_t filterEdgeTriggeredEvents(int epfd, int fd, uint32_t readyEvents, long readableBytes)
{
    constexpr uint32_t EpollEdgeTriggered = 0x80000000u;
    /* EPOLLOUT is reported every time, edge registration or not.  A connected
     * socket never goes back to not-ready as far as WSAPoll is concerned, so an
     * edge filter here would report writability exactly once and then stall. */
    constexpr uint32_t AlwaysReportedEvents = 0x004u | 0x008u | 0x010u; // OUT | ERR | HUP

    std::lock_guard<HostMutex> lock(g_epollMutex);
    EpollInstance *instance = findEpoll(epfd);
    if (!instance)
        return 0;

    auto entry = std::find_if(instance->entries.begin(), instance->entries.end(),
                              [fd](const EpollEntry &candidate) { return candidate.fd == fd; });
    if (entry == instance->entries.end() || !(entry->events & EpollEdgeTriggered))
        return readyEvents;

    const uint32_t edgeEvents = readyEvents & ~AlwaysReportedEvents;
    uint32_t newlyReady = edgeEvents & ~entry->lastReadyEvents;

    /* Linux raises the readable edge when data arrives, even with unread data
     * queued, so one unread byte would deafen a "not-ready -> ready" filter for
     * good.  Compare the queued byte count instead; -1 means unmeasurable. */
    if ((edgeEvents & 0x001u) && readableBytes >= 0)
    {
        if (readableBytes > entry->lastReadableBytes)
            newlyReady |= 0x001u;
        entry->lastReadableBytes = readableBytes;
    }
    else if (readableBytes >= 0)
    {
        entry->lastReadableBytes = readableBytes;
    }

    entry->lastReadyEvents = edgeEvents;
    return newlyReady | (readyEvents & AlwaysReportedEvents);
}

/* Bytes queued on a guest socket, or -1 when that cannot be answered. */
long socketReadableBytes(int guestFd)
{
    const SOCKET host = NetworkBridge::hostSocket(guestFd);
    if (host == INVALID_SOCKET)
        return -1;
    u_long available = 0;
    if (ioctlsocket(host, FIONREAD, &available) != 0)
        return -1;
    return static_cast<long>(available);
}

EventfdInstance *findEventfd(int fd)
{
    for (EventfdInstance &instance : g_eventfdInstances)
    {
        if (instance.used && instance.fd == fd)
            return &instance;
    }
    return nullptr;
}

uint64_t monotonicNanoseconds()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t timerClockNanoseconds(const EventfdInstance &instance)
{
    if (instance.timerClockId == 0) // CLOCK_REALTIME
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }
    return monotonicNanoseconds();
}

bool timerfdReady(const EventfdInstance &instance)
{
    return instance.timerfd && instance.timerArmed &&
           timerClockNanoseconds(instance) >= instance.timerNextNanoseconds;
}

int bridgeEpollCreate(int size)
{
    (void)size;
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EpollInstance &instance : g_epollInstances)
    {
        if (!instance.used)
        {
            instance.used = true;
            instance.fd = FirstEpollDescriptor + static_cast<int>(&instance - g_epollInstances.data());
            instance.entries.clear();
            if (g_epollTraceCount.fetch_add(1) < 16)
                log_debug("ES1 epoll_create -> %d", instance.fd);
            return instance.fd;
        }
    }
    errno = EMFILE;
    return -1;
}

int bridgeEpollControl(int epfd, int operation, int fd, void *event)
{
    (void)epfd;
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EpollInstance *instance = findEpoll(epfd);
    if (!instance || (operation != 2 && !event))
    {
        errno = EBADF;
        return -1;
    }

    const auto *guestEvent = static_cast<const LinuxEpollEvent *>(event);
    if (g_epollTraceCount.fetch_add(1) < 32)
        log_debug("ES1 epoll_ctl epfd=%d op=%d fd=%d events=0x%08x", epfd, operation, fd,
                 guestEvent ? guestEvent->events : 0);
    auto entry = std::find_if(instance->entries.begin(), instance->entries.end(),
                              [fd](const EpollEntry &candidate) { return candidate.fd == fd; });
    if (operation == 1) // EPOLL_CTL_ADD
    {
        if (entry != instance->entries.end())
        {
            errno = EEXIST;
            return -1;
        }
        if (epollTraceEnabled())
            log_info("LLEPOLLCTL ADD epfd=%d fd=%d events=0x%x", epfd, fd, guestEvent->events);
        EpollEntry newEntry = {};
        newEntry.fd = fd;
        newEntry.events = guestEvent->events;
        std::memcpy(newEntry.data, guestEvent->data, sizeof(newEntry.data));
        instance->entries.push_back(newEntry);
        return 0;
    }
    if (operation == 2) // EPOLL_CTL_DEL
    {
        if (entry == instance->entries.end())
        {
            errno = ENOENT;
            return -1;
        }
        instance->entries.erase(entry);
        return 0;
    }
    if (operation == 3) // EPOLL_CTL_MOD
    {
        if (entry == instance->entries.end())
        {
            errno = ENOENT;
            return -1;
        }
        if (epollTraceEnabled())
            log_info("LLEPOLLCTL MOD epfd=%d fd=%d events=0x%x -> 0x%x", epfd, fd,
                     entry->events, guestEvent->events);
        entry->events = guestEvent->events;
        std::memcpy(entry->data, guestEvent->data, sizeof(entry->data));
        entry->lastReadyEvents = 0;
        entry->lastReadableBytes = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int bridgeEpollWait(int epfd, void *events, int maxevents, int timeout)
{
    if (!events || maxevents <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    auto *guestEvents = static_cast<LinuxEpollEvent *>(events);
    const uint64_t startTicks = GetTickCount64();
    if (g_epollTraceCount.fetch_add(1) < 48)
        log_debug("ES1 epoll_wait epfd=%d max=%d timeout=%d", epfd, maxevents, timeout);

    /* Re-read the interest set every pass and never block for long: Asio adds
     * EPOLLOUT from another thread and wakes the reactor through a pipe this
     * shim cannot poll, so a snapshot could miss a queued write forever. */
    for (;;)
    {
        std::vector<EpollEntry> entries;
        {
            std::lock_guard<HostMutex> lock(g_epollMutex);
            EpollInstance *instance = findEpoll(epfd);
            if (!instance)
            {
                errno = EBADF;
                return -1;
            }
            entries = instance->entries;
        }

        std::vector<WSAPOLLFD> hostFds;
        std::vector<size_t> hostIndexes;
        int ready = 0;

        for (size_t index = 0; index < entries.size() && ready < maxevents; ++index)
        {
            const EpollEntry &entry = entries[index];
            EventfdInstance *eventfd = nullptr;
            {
                std::lock_guard<HostMutex> lock(g_epollMutex);
                eventfd = findEventfd(entry.fd);
            }
            if (eventfd)
            {
                uint32_t eventMask = 0;
                {
                    std::lock_guard<HostMutex> lock(g_epollMutex);
                    eventfd = findEventfd(entry.fd);
                    if (eventfd && (entry.events & 0x001) &&
                        (eventfd->timerfd ? timerfdReady(*eventfd) : eventfd->counter != 0))
                        eventMask |= 0x001; // EPOLLIN
                }
                eventMask = filterEdgeTriggeredEvents(epfd, entry.fd, eventMask,
                                                      (eventMask & 0x001u) ? 1 : 0);
                if (eventMask)
                {
                    guestEvents[ready].events = eventMask;
                    std::memcpy(guestEvents[ready].data, entry.data, sizeof(entry.data));
                    ++ready;
                }
                continue;
            }

            const auto *device = VirtualDeviceRegistry::find(entry.fd);
            if (device)
            {
                uint32_t eventMask = 0;
                const int deviceBytes = device->bytesAvailable(entry.fd);
                if ((entry.events & 0x001) && deviceBytes > 0)
                    eventMask |= 0x001; // EPOLLIN
                if (entry.events & 0x004)
                    eventMask |= 0x004; // EPOLLOUT
                eventMask = filterEdgeTriggeredEvents(epfd, entry.fd, eventMask,
                                                      deviceBytes < 0 ? 0 : deviceBytes);
                if (eventMask)
                {
                    guestEvents[ready].events = eventMask;
                    std::memcpy(guestEvents[ready].data, entry.data, sizeof(entry.data));
                    ++ready;
                }
                continue;
            }

            if (NetworkBridge::isSocketDescriptor(entry.fd))
            {
                WSAPOLLFD host = {};
                host.fd = NetworkBridge::hostSocket(entry.fd);
                host.events = winsockEventsFromEpoll(entry.events);
                hostFds.push_back(host);
                hostIndexes.push_back(index);
            }
        }

        if (!hostFds.empty())
        {
            const int slice = ready != 0 ? 0 : 10;
            const int polled =
                WSAPoll(hostFds.data(), static_cast<ULONG>(hostFds.size()), slice);
            if (polled == SOCKET_ERROR)
            {
                errno = WSAGetLastError() == WSAEINTR ? EINTR : EIO;
                return -1;
            }
            for (size_t index = 0; index < hostFds.size() && ready < maxevents; ++index)
            {
                const uint32_t eventMask = epollEventsFromWinsock(hostFds[index].revents);
                const EpollEntry &entry = entries[hostIndexes[index]];
                const uint32_t filteredEvents = filterEdgeTriggeredEvents(
                    epfd, entry.fd, eventMask,
                    (eventMask & 0x001u) ? socketReadableBytes(entry.fd) : -1);
                if (epollTraceEnabled() && ((entry.events & 0x004u) || filteredEvents))
                    log_info("LLEPOLL fd=%d want=0x%x revents=0x%x mask=0x%x filtered=0x%x",
                             entry.fd, entry.events,
                             static_cast<unsigned>(hostFds[index].revents), eventMask,
                             filteredEvents);
                if (!filteredEvents)
                    continue;
                guestEvents[ready].events = filteredEvents;
                std::memcpy(guestEvents[ready].data, entry.data, sizeof(entry.data));
                ++ready;
            }
        }

        if (ready != 0)
        {
            /* A connected socket is writable more or less permanently, so a
             * reactor with nothing to write would spin.  Pace the writable-only
             * case by a millisecond, far below any latency that matters. */
            bool writableOnly = true;
            for (int index = 0; index < ready; ++index)
            {
                if (guestEvents[index].events != 0x004u)
                {
                    writableOnly = false;
                    break;
                }
            }
            if (writableOnly)
                Sleep(1);
            return ready;
        }
        if (timeout == 0)
            return 0;
        if (timeout > 0 && GetTickCount64() - startTicks >= static_cast<uint64_t>(timeout))
            return 0;
        /* Nothing survived the edge filter.  Winsock may have returned at once
         * because a socket is level-ready with a spent edge, so yield rather
         * than coming straight back and spinning a core. */
        if (hostFds.empty())
            Sleep(10);
        else
            Sleep(1);
    }
}

int bridgeEventfd(unsigned int initial, int flags)
{
    // Decline, so Asio takes its pipe fallback.  The game expects a descriptor
    // that behaves like a normal pipe endpoint, which a private type is not.
    (void)initial;
    (void)flags;
    errno = ENOMEM;
    return -1;

#if 0
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EventfdInstance &instance : g_eventfdInstances)
    {
        if (!instance.used)
        {
            instance.used = true;
            instance.fd = FirstEventfdDescriptor + static_cast<int>(&instance - g_eventfdInstances.data());
            instance.semaphore = (flags & 1) != 0; // EFD_SEMAPHORE
            instance.counter = initial;
            if (g_epollTraceCount.fetch_add(1) < 16)
                log_debug("ES1 eventfd -> %d", instance.fd);
            return instance.fd;
        }
    }
    errno = EMFILE;
    return -1;
#endif
}

int bridgeTimerfdCreate(int clockId, int flags)
{
    (void)flags;
    if (clockId != 0 && clockId != 1 && clockId != 4 && clockId != 7)
    {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EventfdInstance &instance : g_eventfdInstances)
    {
        if (!instance.used)
        {
            instance.used = true;
            instance.fd = FirstEventfdDescriptor + static_cast<int>(&instance - g_eventfdInstances.data());
            instance.semaphore = false;
            instance.counter = 0;
            instance.timerfd = true;
            instance.timerClockId = clockId;
            instance.timerArmed = false;
            instance.timerNextNanoseconds = 0;
            instance.timerIntervalNanoseconds = 0;
            log_debug("ES1 timerfd_create -> %d", instance.fd);
            return instance.fd;
        }
    }
    errno = EMFILE;
    return -1;
}

uint64_t timespecNanoseconds(const LinuxTimespec &value)
{
    if (value.seconds < 0 || value.nanoseconds < 0 || value.nanoseconds >= 1000000000)
        return 0;
    return static_cast<uint64_t>(value.seconds) * 1000000000ull +
           static_cast<uint64_t>(value.nanoseconds);
}

LinuxTimespec nanosecondsTimespec(uint64_t value)
{
    LinuxTimespec result{};
    result.seconds = static_cast<int32_t>(value / 1000000000ull);
    result.nanoseconds = static_cast<int32_t>(value % 1000000000ull);
    return result;
}

int bridgeTimerfdSettime(int fd, int flags, const LinuxItimerspec *newValue,
                         LinuxItimerspec *oldValue)
{
    if (!newValue)
    {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance || !instance->timerfd)
    {
        errno = EBADF;
        return -1;
    }

    if (oldValue)
    {
        std::memset(oldValue, 0, sizeof(*oldValue));
        if (instance->timerArmed)
        {
            const uint64_t now = timerClockNanoseconds(*instance);
            const uint64_t remaining = instance->timerNextNanoseconds > now
                                           ? instance->timerNextNanoseconds - now : 0;
            oldValue->value = nanosecondsTimespec(remaining);
            oldValue->interval = nanosecondsTimespec(instance->timerIntervalNanoseconds);
        }
    }

    const uint64_t delay = timespecNanoseconds(newValue->value);
    instance->timerIntervalNanoseconds = timespecNanoseconds(newValue->interval);
    instance->timerArmed = delay != 0;
    if (instance->timerArmed)
    {
        constexpr int TimerAbsolute = 1; // TFD_TIMER_ABSTIME
        instance->timerNextNanoseconds = (flags & TimerAbsolute)
                                               ? delay
                                               : timerClockNanoseconds(*instance) + delay;
        log_debug("ES1 timerfd_settime fd=%d flags=0x%x delay=%llu next=%llu", fd, flags,
                  static_cast<unsigned long long>(delay),
                  static_cast<unsigned long long>(instance->timerNextNanoseconds));
    }
    return 0;
}

int bridgeTimerfdGettime(int fd, LinuxItimerspec *value)
{
    if (!value)
    {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance || !instance->timerfd)
    {
        errno = EBADF;
        return -1;
    }
    std::memset(value, 0, sizeof(*value));
    if (instance->timerArmed)
    {
        const uint64_t now = timerClockNanoseconds(*instance);
        const uint64_t remaining = instance->timerNextNanoseconds > now
                                       ? instance->timerNextNanoseconds - now : 0;
        value->value = nanosecondsTimespec(remaining);
        value->interval = nanosecondsTimespec(instance->timerIntervalNanoseconds);
    }
    return 0;
}

bool isEventfdInternal(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    return findEventfd(fd) != nullptr;
}

int readEventfdInternal(int fd, void *buffer, size_t length)
{
    if (!buffer || length < sizeof(uint64_t))
    {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
    {
        errno = EBADF;
        return -1;
    }
    if (instance->timerfd)
    {
        if (!timerfdReady(*instance))
        {
            errno = EAGAIN;
            return -1;
        }
        const uint64_t now = timerClockNanoseconds(*instance);
        uint64_t expirations = 1;
        if (instance->timerIntervalNanoseconds)
        {
            expirations += (now - instance->timerNextNanoseconds) /
                           instance->timerIntervalNanoseconds;
            instance->timerNextNanoseconds += expirations * instance->timerIntervalNanoseconds;
        }
        else
        {
            instance->timerArmed = false;
        }
        std::memcpy(buffer, &expirations, sizeof(expirations));
        return static_cast<int>(sizeof(expirations));
    }
    if (instance->counter == 0)
    {
        errno = EAGAIN;
        return -1;
    }
    const uint64_t value = instance->semaphore ? 1 : instance->counter;
    std::memcpy(buffer, &value, sizeof(value));
    if (instance->semaphore)
        --instance->counter;
    else
        instance->counter = 0;
    return static_cast<int>(sizeof(value));
}

int writeEventfdInternal(int fd, const void *buffer, size_t length)
{
    if (!buffer || length < sizeof(uint64_t))
    {
        errno = EINVAL;
        return -1;
    }
    uint64_t value = 0;
    std::memcpy(&value, buffer, sizeof(value));
    if (value == UINT64_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
    {
        errno = EBADF;
        return -1;
    }
    if (UINT64_MAX - instance->counter <= value)
    {
        errno = EAGAIN;
        return -1;
    }
    instance->counter += value;
    return static_cast<int>(sizeof(value));
}

int closeEventfdInternal(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    EventfdInstance *instance = findEventfd(fd);
    if (!instance)
        return 0;
    instance->used = false;
    instance->fd = -1;
    instance->counter = 0;
    instance->timerfd = false;
    instance->timerArmed = false;
    instance->timerNextNanoseconds = 0;
    instance->timerIntervalNanoseconds = 0;
    return 0;
}

int bridgeGetNprocs()
{
    const unsigned count = std::thread::hardware_concurrency();
    return count ? static_cast<int>(count) : 1;
}

int bridgeLink(const char *oldPath, const char *newPath)
{
    (void)oldPath;
    (void)newPath;
    return 0;
}

int bridgeMlockall(unsigned long flags)
{
    (void)flags;
    return 0;
}

int bridgeMunlockall()
{
    return 0;
}

long bridgePathconf(const char *path, int name)
{
    (void)path;
    (void)name;
    return 4096;
}

int bridgePthreadKill(void *thread, int signal)
{
    (void)thread;
    (void)signal;
    return 0;
}

struct LinuxDirent64
{
    uint64_t inode;
    int64_t offset;
    uint16_t recordLength;
    uint8_t type;
    char name[260];
};

HostMutex direntMutex;
std::unordered_map<void *, LinuxDirent64> dirent64Records;

LinuxDirent64 *nextDirent64(void *directory)
{
    /* readdir64()/readdir64_r() are Boost.Filesystem's entry points; they need
     * the plain POSIX end-of-directory behaviour, not the WMMT4 quirk that
     * bridgeReaddir() adds. */
    linux_dirent *source = bridgeReaddirPosix(directory);
    if (!source)
    {
        std::lock_guard<HostMutex> lock(direntMutex);
        dirent64Records.erase(directory);
        return nullptr;
    }

    LinuxDirent64 &record = dirent64Records[directory];
    std::memset(&record, 0, sizeof(record));
    record.inode = static_cast<uint32_t>(source->d_ino);
    record.offset = source->d_off;
    record.type = source->d_type;
    const size_t nameLength = std::min(std::strlen(source->d_name), sizeof(record.name) - 1);
    std::memcpy(record.name, source->d_name, nameLength);
    record.name[nameLength] = '\0';
    record.recordLength = static_cast<uint16_t>(offsetof(LinuxDirent64, name) +
                                                std::strlen(record.name) + 1);
    return &record;
}

void *bridgeReaddir64(void *directory)
{
    std::lock_guard<HostMutex> lock(direntMutex);
    return nextDirent64(directory);
}

int bridgeReaddir64R(void *directory, void *entry, void **result)
{
    std::lock_guard<HostMutex> lock(direntMutex);
    LinuxDirent64 *source = nextDirent64(directory);
    if (!source)
    {
        if (result) *result = nullptr;
        return 0;
    }
    if (entry) std::memcpy(entry, source, sizeof(LinuxDirent64));
    if (result) *result = entry;
    return 0;
}

int bridgeStatvfs64(const char *path, void *result)
{
    (void)path;
    if (result) std::memset(result, 0, 256);
    return 0;
}

int bridgeSymlink(const char *target, const char *linkPath)
{
    (void)target;
    (void)linkPath;
    return 0;
}

long bridgeSyscall(long number, ...)
{
    (void)number;
    errno = ENOSYS;
    return -1;
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace Es1CompatBridge
{
/* Forget what this descriptor was last reported ready for.  A fresh socket
 * already polls writable on Windows, so without this the completed connect has
 * no edge left to report. */
void forgetEdgeState(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EpollInstance &instance : g_epollInstances)
    {
        if (!instance.used)
            continue;
        for (EpollEntry &entry : instance.entries)
        {
            if (entry.fd == fd)
                entry.lastReadyEvents = 0;
                entry.lastReadableBytes = 0;
        }
    }
}

/* Drop a closed descriptor from every interest set, as Linux does on the last
 * close() and Asio relies on.  A stale entry makes the next socket to reuse the
 * number fail its ADD with EEXIST. */
void forgetDescriptor(int fd)
{
    std::lock_guard<HostMutex> lock(g_epollMutex);
    for (EpollInstance &instance : g_epollInstances)
    {
        if (!instance.used)
            continue;
        instance.entries.erase(std::remove_if(instance.entries.begin(), instance.entries.end(),
                                              [fd](const EpollEntry &entry) {
                                                  return entry.fd == fd;
                                              }),
                               instance.entries.end());
    }
}

bool isEventfd(int fd)
{
    return isEventfdInternal(fd);
}

int readEventfd(int fd, void *buffer, size_t length)
{
    return readEventfdInternal(fd, buffer, length);
}

int writeEventfd(int fd, const void *buffer, size_t length)
{
    return writeEventfdInternal(fd, buffer, length);
}

int closeEventfd(int fd)
{
    return closeEventfdInternal(fd);
}

void *bridgeTlsGetAddr(const void *tlsIndex)
{
    return GuestTls::GetAddress(tlsIndex);
}

void initBridges()
{
    map("__tls_get_addr", bridgeTlsGetAddr);
    map("___tls_get_addr", bridgeTlsGetAddr);
    map("__strtof_internal", bridgeStrtofInternal);
    map("__strtold_internal", bridgeStrtoldInternal);
    map("abs", bridgeAbs);
    map("epoll_create", bridgeEpollCreate);
    map("epoll_create1", bridgeEpollCreate);
    map("epoll_ctl", bridgeEpollControl);
    map("epoll_wait", bridgeEpollWait);
    map("eventfd", bridgeEventfd);
    map("curl_global_init", bridgeCurlGlobalInit);
    map("curl_global_cleanup", bridgeCurlGlobalCleanup);
    map("curl_easy_init", bridgeCurlEasyInit);
    map("curl_easy_cleanup", bridgeCurlEasyCleanup);
    map("curl_easy_setopt", bridgeCurlEasySetopt);
    map("curl_easy_perform", bridgeCurlEasyPerform);
    map("curl_easy_getinfo", bridgeCurlEasyGetinfo);
    map("timerfd_create", bridgeTimerfdCreate);
    map("timerfd_settime", bridgeTimerfdSettime);
    map("timerfd_gettime", bridgeTimerfdGettime);
    map("get_nprocs", bridgeGetNprocs);
    map("link", bridgeLink);
    map("mlockall", bridgeMlockall);
    map("munlockall", bridgeMunlockall);
    map("pathconf", bridgePathconf);
    map("pthread_kill", bridgePthreadKill);
    map("readdir64", bridgeReaddir64);
    map("readdir64_r", bridgeReaddir64R);
    map("statvfs64", bridgeStatvfs64);
    map("symlink", bridgeSymlink);
    map("syscall", bridgeSyscall);
}
}

#endif
