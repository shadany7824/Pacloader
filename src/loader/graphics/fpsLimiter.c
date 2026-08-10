#include <stdint.h>
#include <stdlib.h>

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
#include "fpsLimiter.h"

double lastTime = 0.0;
int frameCount = 0;
double fps = 0.0;

static int g_fpsLimiterEnabled = 0;
static double g_targetFps = 60.0;

#if defined(_WIN32) || defined(__MINGW32__)
static LARGE_INTEGER g_qpcFrequency;
static LONGLONG g_qpcStart = 0;

/* Keep the video-sync result per thread; WMMT4 waits and presents on different threads. */
static __thread int g_videoSyncWaited = 0;

static LONGLONG qpcNow(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

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
#define SpinMarginMicroseconds 500

/* Optional late-present window; disabled unless LL_PACE_GRACE_MS is set. */
static LONGLONG paceGraceTicks(void)
{
    static int resolved = 0;
    static LONGLONG ticks = 0;

    if (!resolved)
    {
        resolved = 1;
        long milliseconds = 0;
        const char *setting = getenv("LL_PACE_GRACE_MS");
        if (setting && *setting)
        {
            const long value = strtol(setting, NULL, 10);
            if (value >= 0)
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
        (LONGLONG)(((long double)g_qpcFrequency.QuadPart * SpinMarginMicroseconds) /
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
    g_targetFps = getConfig()->fpsTarget > 0.0f ? getConfig()->fpsTarget : 60.0;
    lastTime = 0.0;
    frameCount = 0;
    fps = 0.0;
    g_videoSyncWaited = 0;

    if (!g_fpsLimiterEnabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    QueryPerformanceFrequency(&g_qpcFrequency);
    g_qpcStart = qpcNow();

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

double getTimeInMilliseconds(void)
{
    return (double)clockNow() / 1000.0;
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
    waitUntilQpc(qpcDeadline(index));
    g_videoSyncWaited = 1;
#else
    (void)index;
    g_videoSyncWaited = 1;
#endif
}

int consumeVideoSyncWait(void)
{
    const int waited = g_videoSyncWaited;
    g_videoSyncWaited = 0;
    return waited;
}

void frameTiming(void)
{
    if (!g_fpsLimiterEnabled)
        return;

#if defined(_WIN32) || defined(__MINGW32__)
    /* A thread that already waited for its slot can present immediately. */
    if (consumeVideoSyncWait())
        return;

    /* Pace against the same absolute grid reported by videoSyncCount(). */
    const uint64_t passed = videoSyncCount();
    if (qpcNow() - qpcDeadline(passed) <= paceGraceTicks())
        return;

    waitUntilQpc(qpcDeadline(passed + 1));
#else
    if (consumeVideoSyncWait())
        return;

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
#endif
}

/* Kept as a compatibility entry point for existing callers. The new limiter
 * is deadline-based, so per-frame state is maintained internally. */
void fpsLimiter(FpsLimit *stats)
{
    (void)stats;
    frameTiming();
}
