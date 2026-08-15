#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>
#else
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "../config/config.h"
#include "../log/log.h"
#include "fpsLimiter.h"
#include "../diagnostics/perfProfiler.hpp"

double lastTime = 0.0;
int frameCount = 0;
double fps = 0.0;

static int g_fpsLimiterEnabled = 0;
static double g_targetFps = 60.0;
/* The display paces the swap, so the limiter must not pace it as well. */
static int g_vsyncPresents = 0;

#if defined(_WIN32) || defined(__MINGW32__)
static LARGE_INTEGER g_qpcFrequency;
static LONGLONG g_qpcStart = 0;

/* Per-thread: WMMT4 waits for retrace on one thread and presents on another, and
 * a shared token let the first excuse the second from pacing altogether. */
static __thread int t_videoSyncWaited = 0;

static LONGLONG qpcNow(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

/* The retrace clock the cabinet had: a thread counts real vertical blanks, so the
 * guest's simulation and the scanout share one clock instead of beating. */
typedef struct
{
    UINT hAdapter;
    UINT hDevice;
    UINT vidPnSourceId;
} LlVerticalBlankRequest;

typedef struct
{
    WCHAR deviceName[32];
    UINT hAdapter;
    LUID adapterLuid;
    UINT vidPnSourceId;
} LlOpenAdapterRequest;

typedef LONG(WINAPI *LlOpenAdapterFn)(LlOpenAdapterRequest *);
typedef LONG(WINAPI *LlWaitForVerticalBlankFn)(const LlVerticalBlankRequest *);

static CRITICAL_SECTION g_vblankLock;
static CONDITION_VARIABLE g_vblankSignal;
static uint64_t g_vblankCount = 0;
static int g_vblankClockActive = 0;
static volatile LONG g_vblankStop = 0;
static HANDLE g_vblankThread = NULL;
static void *g_vblankWindow = NULL;
static LlWaitForVerticalBlankFn g_waitForVerticalBlank = NULL;
static LlVerticalBlankRequest g_vblankRequest;
static unsigned g_vblankDivisor = 1;

static DWORD WINAPI vblankThreadProc(void *unused)
{
    (void)unused;
    unsigned raw = 0;
    while (!InterlockedCompareExchange(&g_vblankStop, 0, 0))
    {
        /* Failure is permanent in practice; stop counting and let waiters fall back. */
        if (g_waitForVerticalBlank(&g_vblankRequest) < 0)
            break;

        if (++raw < g_vblankDivisor)
            continue;
        raw = 0;

        EnterCriticalSection(&g_vblankLock);
        ++g_vblankCount;
        LeaveCriticalSection(&g_vblankLock);
        WakeAllConditionVariable(&g_vblankSignal);
    }

    EnterCriticalSection(&g_vblankLock);
    g_vblankClockActive = 0;
    LeaveCriticalSection(&g_vblankLock);
    WakeAllConditionVariable(&g_vblankSignal);
    return 0;
}

static int startVerticalBlankClock(void);

void setVideoSyncWindow(void *window)
{
    g_vblankWindow = window;
    /* Starts here, not in initFpsLimiter(): it needs the window's monitor. */
    if (g_vsyncPresents && !g_vblankClockActive)
        startVerticalBlankClock();
}

/* Returns non-zero when the display clock took over from the grid. */
static int startVerticalBlankClock(void)
{
    if (!g_vblankWindow)
        return 0;

    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi)
        return 0;

    LlOpenAdapterFn openAdapter =
        (LlOpenAdapterFn)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromGdiDisplayName");
    g_waitForVerticalBlank =
        (LlWaitForVerticalBlankFn)(void *)GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent");
    if (!openAdapter || !g_waitForVerticalBlank)
        return 0;

    /* The window's monitor, not the primary: they can run at different rates. */
    HMONITOR monitor = MonitorFromWindow((HWND)g_vblankWindow, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW monitorInfo;
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, (LPMONITORINFO)&monitorInfo))
        return 0;

    DEVMODEW mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode) ||
        mode.dmDisplayFrequency == 0)
        return 0;

    const double refresh = (double)mode.dmDisplayFrequency;
    const long divisor = lround(refresh / g_targetFps);
    if (divisor < 1 || fabs(refresh / (double)divisor - g_targetFps) > 1.0)
    {
        log_warn("Vertical blank clock: %ls runs at %.0f Hz, which is not a multiple of "
                 "the %.2f Hz target; keeping the QPC grid",
                 monitorInfo.szDevice, refresh, g_targetFps);
        return 0;
    }
    g_vblankDivisor = (unsigned)divisor;

    LlOpenAdapterRequest request;
    ZeroMemory(&request, sizeof(request));
    wcsncpy(request.deviceName, monitorInfo.szDevice,
            sizeof(request.deviceName) / sizeof(request.deviceName[0]) - 1);
    if (openAdapter(&request) < 0)
        return 0;

    ZeroMemory(&g_vblankRequest, sizeof(g_vblankRequest));
    g_vblankRequest.hAdapter = request.hAdapter;
    g_vblankRequest.vidPnSourceId = request.vidPnSourceId;

    InterlockedExchange(&g_vblankStop, 0);
    g_vblankCount = 0;
    g_vblankClockActive = 1;
    g_vblankThread = CreateThread(NULL, 0, vblankThreadProc, NULL, 0, NULL);
    if (!g_vblankThread)
    {
        g_vblankClockActive = 0;
        return 0;
    }

    log_warn("Vertical blank clock: %ls at %.0f Hz, %.2f Hz target, counting every %u blank",
             monitorInfo.szDevice, refresh, g_targetFps, g_vblankDivisor);
    return 1;
}

/* Do not infer vertical blanks from completed swaps: a present-driven counter
 * deadlocks on loading screens, and patching that with QPC extrapolation
 * measured 2.6 presents a second against 48.9. Count real blanks above. */

static LONGLONG qpcDeadline(uint64_t frameIndex)
{
    /* Keep the fractional part of non-integer frame periods instead of
     * truncating the target to whole microseconds. */
    const long double ticks =
        (long double)g_qpcStart +
        ((long double)frameIndex * (long double)g_qpcFrequency.QuadPart) / g_targetFps;
    return (LONGLONG)ticks;
}

/* Sleep through the frame and spin only during the final timing window. */
#define DefaultSpinMarginMicroseconds 100
static LONGLONG g_spinMarginMicroseconds = DefaultSpinMarginMicroseconds;

/* How late a frame may still present. Measured worse than holding to the grid -
 * a released present keeps its shifted phase and the error accumulates. */
#define DefaultPaceGraceMilliseconds 0.0
static LONGLONG paceGraceTicks(void)
{
    static int resolved = 0;
    static LONGLONG ticks = 0;

    if (!resolved)
    {
        resolved = 1;
        double milliseconds = DefaultPaceGraceMilliseconds;
        const char *setting = getenv("LL_PACE_GRACE_MS");
        if (setting && *setting)
        {
            const double value = strtod(setting, NULL);
            if (value >= 0.0)
                milliseconds = value;
        }
        ticks = (LONGLONG)(((long double)g_qpcFrequency.QuadPart * milliseconds) / 1000.0L);
    }
    return ticks;
}

static int g_coarseSleepIsTimer = 0;

/* Created per waiting thread: a timer object cannot be shared by two waiters. */
static HANDLE waitTimer(void)
{
    static __thread HANDLE timer = NULL;
    static __thread int created = 0;

    if (!created)
    {
        created = 1;
        if (g_coarseSleepIsTimer)
            timer = CreateWaitableTimerExW(NULL, NULL,
                                           CREATE_WAITABLE_TIMER_MANUAL_RESET |
                                               CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                           TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    return timer;
}

static void waitUntilQpc(LONGLONG deadline)
{
    const LONGLONG spinMargin =
        (LONGLONG)(((long double)g_qpcFrequency.QuadPart * g_spinMarginMicroseconds) /
                   1000000.0L);
    HANDLE timer = waitTimer();

    for (;;)
    {
        const LONGLONG remaining = deadline - qpcNow();
        if (remaining <= 0)
            return;

        /* Sleep through the bulk of the frame and spin only the final fraction. */
        if (remaining <= spinMargin)
        {
            _mm_pause();
            continue;
        }

        if (timer)
        {
            const LONGLONG sleepTicks = remaining - spinMargin;
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(((long double)sleepTicks * 10000000.0L) /
                                       (long double)g_qpcFrequency.QuadPart);
            if (due.QuadPart >= 0)
                due.QuadPart = -1;
            if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
            {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }

        /* timeBeginPeriod(1) keeps Sleep(1) within the spin margin. */
        Sleep(1);
    }
}
#endif

void initFpsLimiter(void)
{
    g_fpsLimiterEnabled = getConfig()->fpsLimiter == 1;
    g_vsyncPresents = g_fpsLimiterEnabled && getConfig()->vsync != 0;
    g_targetFps = getConfig()->fpsTarget > 0.0f ? getConfig()->fpsTarget : 60.0;
    lastTime = 0.0;
    frameCount = 0;
    fps = 0.0;
    t_videoSyncWaited = 0;

    if (!g_fpsLimiterEnabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    g_spinMarginMicroseconds = DefaultSpinMarginMicroseconds;
    const char *setting = getenv("LL_PACE_SPIN_US");
    if (setting)
    {
        const long value = strtol(setting, NULL, 10);
        if (value >= 0 && value <= 2000)
            g_spinMarginMicroseconds = (LONGLONG)value;
    }

    QueryPerformanceFrequency(&g_qpcFrequency);
    g_qpcStart = qpcNow();

    static int vblankLockReady = 0;
    if (!vblankLockReady)
    {
        vblankLockReady = 1;
        InitializeCriticalSection(&g_vblankLock);
        InitializeConditionVariable(&g_vblankSignal);
    }

    /* Use the high-resolution wait when the host provides it. */
    if (!g_coarseSleepIsTimer)
    {
        HANDLE probe = CreateWaitableTimerExW(NULL, NULL,
                                              CREATE_WAITABLE_TIMER_MANUAL_RESET |
                                                  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (probe)
        {
            g_coarseSleepIsTimer = 1;
            CloseHandle(probe);
        }
        else
        {
            timeBeginPeriod(1);
        }
    }
#endif
}

double getTimeInSeconds(void)
{
    return (double)clockNow() / 1000000.0;
}

double calculateFps(void)
{
    const double currentTime = getTimeInSeconds();
    const double deltaTime = currentTime - lastTime;
    frameCount++;
    if (deltaTime >= 1.0)
    {
        fps = frameCount / deltaTime;
        frameCount = 0;
        lastTime = currentTime;
    }
    return fps;
}

int64_t clockNow(void)
{
#if defined(_WIN32) || defined(__MINGW32__)
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    LARGE_INTEGER now;

    if (!initialized)
    {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    QueryPerformanceCounter(&now);
    return (int64_t)(((long double)now.QuadPart * 1000000.0L) /
                     (long double)frequency.QuadPart);
#else
    struct timeval timeNow;
    gettimeofday(&timeNow, NULL);
    return (int64_t)timeNow.tv_sec * 1000000 + timeNow.tv_usec;
#endif
}

/*
 * The retrace counter and its wait share the limiter's grid so a guest that
 * paces itself on glXWaitVideoSyncSGI and the limiter do not wait against two
 * origins, which costs a fraction of a frame every frame.
 */
uint64_t videoSyncCount(void)
{
#if defined(_WIN32) || defined(__MINGW32__)
    if (g_qpcFrequency.QuadPart == 0)
        return 0;

    if (g_vblankClockActive)
    {
        EnterCriticalSection(&g_vblankLock);
        const uint64_t count = g_vblankCount;
        LeaveCriticalSection(&g_vblankLock);
        return count;
    }

    const LONGLONG elapsed = qpcNow() - g_qpcStart;
    if (elapsed <= 0)
        return 0;
    return (uint64_t)((long double)elapsed * (long double)g_targetFps /
                      (long double)g_qpcFrequency.QuadPart);
#else
    return (uint64_t)((double)clockNow() * g_targetFps / 1000000.0);
#endif
}

void waitVideoSync(uint64_t index)
{
#if defined(_WIN32) || defined(__MINGW32__)
    if (g_qpcFrequency.QuadPart == 0)
        return;

    if (g_vblankClockActive)
    {
        EnterCriticalSection(&g_vblankLock);
        /* Timed: a clock that stops must cost a late frame, not a hung guest. */
        while (g_vblankClockActive && g_vblankCount < index)
            SleepConditionVariableCS(&g_vblankSignal, &g_vblankLock, 50);
        const int served = g_vblankClockActive;
        LeaveCriticalSection(&g_vblankLock);
        if (served)
        {
            t_videoSyncWaited = 1;
            return;
        }
    }

    waitUntilQpc(qpcDeadline(index));
#else
    (void)index;
#endif
    t_videoSyncWaited = 1;
}

int consumeVideoSyncWait(void)
{
    const int waited = t_videoSyncWaited;
    t_videoSyncWaited = 0;
    return waited;
}

/* Subtracting the swap cost from the deadline so the swap finishes on the slot
 * was measured worse (p95 18.5 -> 20.1 ms): it unpins the present from the grid. */

void frameTiming(void)
{
    uint64_t profileStart = PerfProfiler_Begin("Frame", "frameTiming");
    if (!g_fpsLimiterEnabled)
    {
        PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
        return;
    }

#if defined(_WIN32) || defined(__MINGW32__)
    /* The swap already blocks on the scanout; pacing here too stacks two waits. */
    if (g_vsyncPresents)
    {
        consumeVideoSyncWait();
        PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
        return;
    }

    /* Any thread that did not wait for its own slot still has to be paced. */
    if (consumeVideoSyncWait())
    {
        PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
        return;
    }

    /* Pace against the same absolute grid reported by videoSyncCount(). */
    const uint64_t passed = videoSyncCount();
    if (qpcNow() - qpcDeadline(passed) <= paceGraceTicks())
    {
        PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
        return;
    }

    waitUntilQpc(qpcDeadline(passed + 1));
    PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
#else
    if (consumeVideoSyncWait())
    {
        PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
        return;
    }

    const int64_t targetMicroseconds = (int64_t)(1000000.0 / g_targetFps);
    static int64_t nextDeadline = 0;
    const int64_t now = clockNow();

    if (nextDeadline == 0)
        nextDeadline = now;
    nextDeadline += targetMicroseconds;

    while ((nextDeadline - clockNow()) > 0)
    {
        const int64_t remaining = nextDeadline - clockNow();
        struct timespec request;
        request.tv_sec = (time_t)(remaining / 1000000);
        request.tv_nsec = (long)(remaining % 1000000) * 1000L;
        nanosleep(&request, NULL);
    }

    if (clockNow() > nextDeadline + targetMicroseconds)
        nextDeadline = clockNow();
    PerfProfiler_End("Frame", "frameTiming", profileStart, 0);
#endif
}

