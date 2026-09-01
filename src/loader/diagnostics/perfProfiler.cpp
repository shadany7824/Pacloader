#include "perfProfiler.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <cstdint>

namespace
{
constexpr size_t kEntryCount = 1024;
constexpr size_t kFrameSampleCount = 4096;
constexpr size_t kFrameSpikeCount = 2048;
constexpr size_t kPresentSpikeCount = 4096;
constexpr size_t kTextureQueryStatCount = 128;

struct Entry
{
    const char *domain = nullptr;
    const char *name = nullptr;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> ticks{0};
    std::atomic<uint64_t> maxTicks{0};
    std::atomic<uint64_t> bytes{0};
};

struct ThreadData
{
    DWORD threadId = 0;
    std::atomic<size_t> used{0};
    std::array<Entry, kEntryCount> entries{};
};

constexpr size_t kGpuSampleCount = 4096;

struct GpuStats
{
    std::atomic<uint64_t> samples{0};
    std::atomic<uint64_t> busyNs[kGpuSampleCount]{};
    std::atomic<int64_t> backlogNs[kGpuSampleCount]{};
};

GpuStats g_gpuStats;

struct ExtraStats
{
    std::atomic<uint64_t> cacheSkips{0};
    std::atomic<uint64_t> frameSamples{0};
    std::atomic<uint64_t> frameTicks[kFrameSampleCount]{};
};

struct TextureQueryStat
{
    std::atomic<uint32_t> pname{0};
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> hits{0};
};

struct FrameSpike
{
    uint64_t sequence = 0;
    uint64_t totalTicks = 0;
    uint64_t glTicks = 0;
    uint64_t pthreadTicks = 0;
    uint64_t presentTicks = 0;
    uint64_t glxTicks = 0;
    uint64_t otherTicks = 0;
    uint64_t glCalls = 0;
    uint64_t pthreadCalls = 0;
    uint64_t presentCalls = 0;
    uint64_t glxCalls = 0;
    uint64_t worstTicks = 0;
    const char *worstDomain = nullptr;
    const char *worstName = nullptr;
};

struct FrameAccumulator
{
    std::atomic<uint64_t> glTicks{0};
    std::atomic<uint64_t> pthreadTicks{0};
    std::atomic<uint64_t> presentTicks{0};
    std::atomic<uint64_t> glxTicks{0};
    std::atomic<uint64_t> otherTicks{0};
    std::atomic<uint64_t> glCalls{0};
    std::atomic<uint64_t> pthreadCalls{0};
    std::atomic<uint64_t> presentCalls{0};
    std::atomic<uint64_t> glxCalls{0};
    /* The frame's most expensive call. Racy by design: a diagnostic can afford
     * that far more than every profiled call can afford a lock. */
    std::atomic<uint64_t> worstTicks{0};
    std::atomic<const char *> worstDomain{nullptr};
    std::atomic<const char *> worstName{nullptr};
};

struct PresentSpike
{
    char source[8]{};
    uint64_t sequence = 0;
    uint64_t totalTicks = 0;
    uint64_t eventTicks = 0;
    uint64_t pacingTicks = 0;
    uint64_t swapTicks = 0;
    uint64_t intervalTicks = 0;
    uint64_t gapTicks = 0;
};

/* The spacing between marks, which is what the display sees. Per-frame work
 * totals cannot show pacing jitter; the gaps between frames can. */
constexpr size_t kIntervalSeriesCount = 8;
constexpr size_t kIntervalSampleCount = 16384;
constexpr size_t kIntervalBucketCount = 24;
constexpr uint64_t kIntervalBucketMicroseconds = 2000;

struct IntervalSeries
{
    const char *name = nullptr;
    std::atomic<uint64_t> last{0};
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> buckets[kIntervalBucketCount]{};
    std::atomic<uint64_t> samples[kIntervalSampleCount]{};
};

IntervalSeries g_intervalSeries[kIntervalSeriesCount];
std::atomic<size_t> g_intervalSeriesCount{0};
std::mutex g_intervalSeriesLock;

std::once_flag g_initOnce;
std::atomic<int> g_enabledState{-1};
bool g_enabled = false;
LARGE_INTEGER g_frequency{};
std::mutex g_threadsLock;
std::mutex g_writeLock;
/* WMMT4 runs well over 64 threads, and a missed thread is absent from the report. */
constexpr size_t kMaxThreads = 192;
ThreadData *g_threads[kMaxThreads]{};
size_t g_threadCount = 0;
DWORD g_tlsIndex = TLS_OUT_OF_INDEXES;
std::atomic<bool> g_runtimeReady{false};
std::atomic<bool> g_flushStop{false};
HANDLE g_flushThread = nullptr;
bool g_backgroundFlushEnabled = true;
ExtraStats g_extraStats;
TextureQueryStat g_textureQueryStats[kTextureQueryStatCount]{};
FrameAccumulator g_frameAccumulator;
std::atomic<bool> g_frameBoundaryActive{false};
std::atomic<uint64_t> g_frameBoundaryStart{0};
std::atomic<uint64_t> g_frameSequence{0};
std::atomic<uint64_t> g_presentSequence{0};
std::mutex g_frameSpikeLock;
FrameSpike g_frameSpikes[kFrameSpikeCount]{};
/* Totals, not array lengths: the arrays are rings, so these keep counting past
 * their capacity and the writers take the last min(total, capacity) entries. */
uint64_t g_frameSpikeCount = 0;
uint64_t g_frameSpikeThresholdTicks = 0;
std::mutex g_presentSpikeLock;
PresentSpike g_presentSpikes[kPresentSpikeCount]{};
uint64_t g_presentSpikeCount = 0;
std::atomic<uint64_t> g_lastPresentEnd{0};

constexpr size_t kBufferSiteCount = 1024;
constexpr size_t kBufferPatternCount = 1024;
constexpr size_t kBufferObjectCount = 8192;

enum class BufferOperation : uint8_t
{
    Gen = 1,
    Data = 2,
    Delete = 3,
    Bind = 4,
};

struct BufferSiteEntry
{
    bool valid = false;
    BufferOperation operation = BufferOperation::Gen;
    uintptr_t caller = 0;
    uintptr_t context = 0;
    uint32_t target = 0;
    uint32_t usage = 0;
    uint64_t size = 0;
    uint64_t calls = 0;
    uint64_t bytes = 0;
};

struct BufferPatternEntry
{
    bool valid = false;
    uintptr_t context = 0;
    uint32_t target = 0;
    uint32_t usage = 0;
    uint64_t size = 0;
    uint64_t dataCalls = 0;
    uint64_t deletedObjects = 0;
    uint64_t reusedNames = 0;
    uint64_t objectsWithData = 0;
    uint64_t bindCalls = 0;
    uint64_t objectsWithBinds = 0;
};

struct BufferObjectEntry
{
    bool valid = false;
    bool active = false;
    bool everDeleted = false;
    uintptr_t context = 0;
    uint32_t buffer = 0;
    uint32_t target = 0;
    uint32_t usage = 0;
    uint64_t size = 0;
    uint64_t dataCalls = 0;
    uint64_t bindCalls = 0;
};

std::mutex g_bufferTraceLock;
BufferSiteEntry g_bufferSites[kBufferSiteCount]{};
BufferPatternEntry g_bufferPatterns[kBufferPatternCount]{};
BufferObjectEntry g_bufferObjects[kBufferObjectCount]{};
bool g_bufferTraceEnabled = false;

uint64_t bufferHash(BufferOperation operation, uintptr_t caller, uintptr_t context,
                    uint32_t target, uint32_t usage, uint64_t size)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = (hash ^ static_cast<uint64_t>(operation)) * 1099511628211ULL;
    hash = (hash ^ static_cast<uint64_t>(caller)) * 1099511628211ULL;
    hash = (hash ^ static_cast<uint64_t>(context)) * 1099511628211ULL;
    hash = (hash ^ target) * 1099511628211ULL;
    hash = (hash ^ usage) * 1099511628211ULL;
    hash = (hash ^ size) * 1099511628211ULL;
    return hash;
}

BufferSiteEntry *findBufferSite(BufferOperation operation, uintptr_t caller,
                                uintptr_t context, uint32_t target, uint32_t usage,
                                uint64_t size)
{
    const size_t first = static_cast<size_t>(bufferHash(operation, caller, context,
                                                        target, usage, size) % kBufferSiteCount);
    for (size_t probe = 0; probe < kBufferSiteCount; ++probe)
    {
        BufferSiteEntry &entry = g_bufferSites[(first + probe) % kBufferSiteCount];
        if (!entry.valid)
        {
            entry.valid = true;
            entry.operation = operation;
            entry.caller = caller;
            entry.context = context;
            entry.target = target;
            entry.usage = usage;
            entry.size = size;
            return &entry;
        }
        if (entry.operation == operation && entry.caller == caller &&
            entry.context == context && entry.target == target &&
            entry.usage == usage && entry.size == size)
            return &entry;
    }
    return nullptr;
}

BufferPatternEntry *findBufferPattern(uintptr_t context, uint32_t target,
                                      uint32_t usage, uint64_t size)
{
    const size_t first = static_cast<size_t>(bufferHash(BufferOperation::Data, 0, context,
                                                        target, usage, size) % kBufferPatternCount);
    for (size_t probe = 0; probe < kBufferPatternCount; ++probe)
    {
        BufferPatternEntry &entry = g_bufferPatterns[(first + probe) % kBufferPatternCount];
        if (!entry.valid)
        {
            entry.valid = true;
            entry.context = context;
            entry.target = target;
            entry.usage = usage;
            entry.size = size;
            return &entry;
        }
        if (entry.context == context && entry.target == target &&
            entry.usage == usage && entry.size == size)
            return &entry;
    }
    return nullptr;
}

BufferObjectEntry *findBufferObject(uintptr_t context, uint32_t buffer, bool create)
{
    const size_t first = static_cast<size_t>(bufferHash(BufferOperation::Gen, 0, context,
                                                        0, 0, buffer) % kBufferObjectCount);
    for (size_t probe = 0; probe < kBufferObjectCount; ++probe)
    {
        BufferObjectEntry &entry = g_bufferObjects[(first + probe) % kBufferObjectCount];
        if (!entry.valid)
        {
            if (!create)
                return nullptr;
            entry.valid = true;
            entry.context = context;
            entry.buffer = buffer;
            return &entry;
        }
        if (entry.context == context && entry.buffer == buffer)
            return &entry;
    }
    return nullptr;
}

const char *bufferOperationName(BufferOperation operation)
{
    switch (operation)
    {
    case BufferOperation::Gen: return "glGenBuffers";
    case BufferOperation::Data: return "glBufferData";
    case BufferOperation::Delete: return "glDeleteBuffers";
    case BufferOperation::Bind: return "glBindBuffer";
    }
    return "unknown";
}

void writeBufferTraceReport(const std::function<bool(const char *, size_t)> &writeText,
                            bool &ok)
{
    if (!g_bufferTraceEnabled)
        return;

    std::lock_guard<std::mutex> lock(g_bufferTraceLock);
    for (const BufferSiteEntry &entry : g_bufferSites)
    {
        if (!ok || !entry.valid || entry.calls == 0)
            continue;
        char line[512];
        const int length = std::snprintf(
            line, sizeof(line), "extra,GLBufferSite,%s,0x%llx,0x%llx,0x%08lx,0x%08lx,%llu,%llu,%llu\r\n",
            bufferOperationName(entry.operation),
            static_cast<unsigned long long>(entry.caller),
            static_cast<unsigned long long>(entry.context),
            static_cast<unsigned long>(entry.target), static_cast<unsigned long>(entry.usage),
            static_cast<unsigned long long>(entry.size),
            static_cast<unsigned long long>(entry.calls),
            static_cast<unsigned long long>(entry.bytes));
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
    }

    for (const BufferPatternEntry &entry : g_bufferPatterns)
    {
        if (!ok || !entry.valid ||
            (entry.dataCalls == 0 && entry.deletedObjects == 0 && entry.reusedNames == 0 &&
             entry.bindCalls == 0))
            continue;
        char line[512];
        const int length = std::snprintf(
            line, sizeof(line), "extra,GLBufferPattern,0x%llx,0x%08lx,0x%08lx,%llu,%llu,%llu,%llu,%llu,%llu,%llu\r\n",
            static_cast<unsigned long long>(entry.context),
            static_cast<unsigned long>(entry.target), static_cast<unsigned long>(entry.usage),
            static_cast<unsigned long long>(entry.size),
            static_cast<unsigned long long>(entry.dataCalls),
            static_cast<unsigned long long>(entry.deletedObjects),
            static_cast<unsigned long long>(entry.reusedNames),
            static_cast<unsigned long long>(entry.objectsWithData),
            static_cast<unsigned long long>(entry.bindCalls),
            static_cast<unsigned long long>(entry.objectsWithBinds));
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
    }
}

void resetFrameAccumulator()
{
    g_frameAccumulator.glTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.pthreadTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.presentTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.glxTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.otherTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.glCalls.store(0, std::memory_order_relaxed);
    g_frameAccumulator.pthreadCalls.store(0, std::memory_order_relaxed);
    g_frameAccumulator.presentCalls.store(0, std::memory_order_relaxed);
    g_frameAccumulator.glxCalls.store(0, std::memory_order_relaxed);
    g_frameAccumulator.worstTicks.store(0, std::memory_order_relaxed);
    g_frameAccumulator.worstDomain.store(nullptr, std::memory_order_relaxed);
    g_frameAccumulator.worstName.store(nullptr, std::memory_order_relaxed);
}

/* These are supposed to take most of a frame; leaving them in the worst-call
 * search only ever nominates the wait. */
bool deliberateWait(const char *domain, const char *name)
{
    if (!domain || !name)
        return false;
    if (std::strcmp(domain, "Frame") == 0)
        return true;
    return std::strcmp(domain, "GLX") == 0 &&
           (std::strcmp(name, "bridgeGlxWaitVideoSyncSGI") == 0 ||
            std::strcmp(name, "bridgeGlxSwapBuffers") == 0);
}

void addFrameEvent(const char *domain, const char *name, uint64_t elapsed)
{
    if (!g_frameBoundaryActive.load(std::memory_order_acquire))
        return;

    if (!deliberateWait(domain, name) &&
        elapsed > g_frameAccumulator.worstTicks.load(std::memory_order_relaxed))
    {
        g_frameAccumulator.worstTicks.store(elapsed, std::memory_order_relaxed);
        g_frameAccumulator.worstDomain.store(domain, std::memory_order_relaxed);
        g_frameAccumulator.worstName.store(name, std::memory_order_relaxed);
    }

    if (domain && std::strcmp(domain, "GL") == 0)
    {
        g_frameAccumulator.glTicks.fetch_add(elapsed, std::memory_order_relaxed);
        g_frameAccumulator.glCalls.fetch_add(1, std::memory_order_relaxed);
    }
    else if (domain && std::strcmp(domain, "PthreadMap") == 0)
    {
        g_frameAccumulator.pthreadTicks.fetch_add(elapsed, std::memory_order_relaxed);
        g_frameAccumulator.pthreadCalls.fetch_add(1, std::memory_order_relaxed);
    }
    else if (domain && std::strcmp(domain, "Present") == 0)
    {
        g_frameAccumulator.presentTicks.fetch_add(elapsed, std::memory_order_relaxed);
        g_frameAccumulator.presentCalls.fetch_add(1, std::memory_order_relaxed);
    }
    else if (domain && std::strcmp(domain, "GLX") == 0)
    {
        g_frameAccumulator.glxTicks.fetch_add(elapsed, std::memory_order_relaxed);
        g_frameAccumulator.glxCalls.fetch_add(1, std::memory_order_relaxed);
    }
    /* frameTiming is the limiter waiting on purpose; counting it as "other" put
     * ~8 ms of phantom work on every spike row. */
    else if (!deliberateWait(domain, name))
    {
        g_frameAccumulator.otherTicks.fetch_add(elapsed, std::memory_order_relaxed);
    }
}

void writeGpuReport(const std::function<bool(const char *, size_t)> &writeText, bool &ok)
{
    const uint64_t samples = g_gpuStats.samples.load(std::memory_order_relaxed);
    if (samples == 0)
        return;

    /* Static: writeReport already carries a 32 KB frame array, and this runs
     * under the write lock. */
    static int64_t sorted[kGpuSampleCount];
    const size_t count =
        static_cast<size_t>(samples > kGpuSampleCount ? kGpuSampleCount : samples);
    char line[256];

    auto report = [&](const char *label) {
        for (size_t i = 1; i < count; ++i)
        {
            const int64_t value = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j - 1] > value)
            {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = value;
        }
        /* The GPU clock is already nanoseconds, so no QPC scaling here. */
        const int length = std::snprintf(
            line, sizeof(line), "extra,GpuStats,%s,%llu/%.3f/%.3f/%.3f,,,,\r\n", label,
            static_cast<unsigned long long>(count),
            sorted[(count - 1) * 50 / 100] / 1000.0, sorted[(count - 1) * 95 / 100] / 1000.0,
            sorted[(count - 1) * 99 / 100] / 1000.0);
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
    };

    for (size_t i = 0; i < count; ++i)
        sorted[i] = static_cast<int64_t>(g_gpuStats.busyNs[i].load(std::memory_order_relaxed));
    report("busy_p50_p95_p99_us");

    if (!ok)
        return;
    for (size_t i = 0; i < count; ++i)
        sorted[i] = g_gpuStats.backlogNs[i].load(std::memory_order_relaxed);
    report("backlog_p50_p95_p99_us");
}

void writeFrameSpikeReport(const std::function<bool(const char *, size_t)> &writeText,
                           bool &ok)
{
    std::lock_guard<std::mutex> lock(g_frameSpikeLock);
    const double scale = 1000000.0 / static_cast<double>(g_frequency.QuadPart);
    char summary[256];
    const int summaryLength = std::snprintf(
        summary, sizeof(summary), "extra,FrameSpikeStats,threshold_us,%.3f,count,%llu\r\n",
        g_frameSpikeThresholdTicks * scale,
        static_cast<unsigned long long>(g_frameSpikeCount));
    ok = summaryLength > 0 && static_cast<size_t>(summaryLength) < sizeof(summary) &&
         writeText(summary, static_cast<size_t>(summaryLength));
    const uint64_t frameStored = g_frameSpikeCount < kFrameSpikeCount ? g_frameSpikeCount
                                                                      : kFrameSpikeCount;
    const uint64_t frameFirst = g_frameSpikeCount - frameStored;
    for (uint64_t i = 0; ok && i < frameStored; ++i)
    {
        const FrameSpike &spike = g_frameSpikes[(frameFirst + i) % kFrameSpikeCount];
        char line[768];
        const int length = std::snprintf(
            line, sizeof(line),
            "extra,FrameSpike,%llu,total_us,%.3f,gl_us,%.3f,pthread_us,%.3f,present_us,%.3f,"
            "glx_us,%.3f,other_us,%.3f,gl_calls,%llu,pthread_calls,%llu,present_calls,%llu,"
            "glx_calls,%llu,worst,%s/%s,worst_us,%.3f\r\n",
            static_cast<unsigned long long>(spike.sequence),
            spike.totalTicks * scale, spike.glTicks * scale,
            spike.pthreadTicks * scale, spike.presentTicks * scale,
            spike.glxTicks * scale, spike.otherTicks * scale,
            static_cast<unsigned long long>(spike.glCalls),
            static_cast<unsigned long long>(spike.pthreadCalls),
            static_cast<unsigned long long>(spike.presentCalls),
            static_cast<unsigned long long>(spike.glxCalls),
            spike.worstDomain ? spike.worstDomain : "-",
            spike.worstName ? spike.worstName : "-",
            spike.worstTicks * scale);
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
    }
}

void writePresentSpikeReport(const std::function<bool(const char *, size_t)> &writeText,
                             bool &ok)
{
    std::lock_guard<std::mutex> lock(g_presentSpikeLock);
    char summary[256];
    const int summaryLength = std::snprintf(
        summary, sizeof(summary), "extra,PresentSpikeStats,threshold_us,%.3f,count,%llu\r\n",
        g_frameSpikeThresholdTicks * 1000000.0 /
            static_cast<double>(g_frequency.QuadPart),
        static_cast<unsigned long long>(g_presentSpikeCount));
    ok = summaryLength > 0 && static_cast<size_t>(summaryLength) < sizeof(summary) &&
         writeText(summary, static_cast<size_t>(summaryLength));

    const double scale = 1000000.0 / static_cast<double>(g_frequency.QuadPart);
    const uint64_t presentStored =
        g_presentSpikeCount < kPresentSpikeCount ? g_presentSpikeCount : kPresentSpikeCount;
    const uint64_t presentFirst = g_presentSpikeCount - presentStored;
    for (uint64_t i = 0; ok && i < presentStored; ++i)
    {
        const PresentSpike &spike = g_presentSpikes[(presentFirst + i) % kPresentSpikeCount];
        const uint64_t accounted = spike.eventTicks + spike.pacingTicks + spike.swapTicks;
        const uint64_t other = spike.totalTicks > accounted ? spike.totalTicks - accounted : 0;
        char line[512];
        const int length = std::snprintf(
            line, sizeof(line),
            "extra,PresentSpike,%s,%llu,interval_us,%.3f,gap_us,%.3f,total_us,%.3f,"
            "event_us,%.3f,pacing_us,%.3f,swap_us,%.3f,other_us,%.3f\r\n",
            spike.source, static_cast<unsigned long long>(spike.sequence),
            spike.intervalTicks * scale, spike.gapTicks * scale,
            spike.totalTicks * scale, spike.eventTicks * scale,
            spike.pacingTicks * scale, spike.swapTicks * scale,
            other * scale);
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
    }
}

void writeIntervalReport(const std::function<bool(const char *, size_t)> &writeText,
                         bool &ok)
{
    const size_t seriesCount = g_intervalSeriesCount.load(std::memory_order_acquire);
    const double scale = 1000000.0 / static_cast<double>(g_frequency.QuadPart);
    static uint64_t sorted[kIntervalSampleCount];

    for (size_t s = 0; ok && s < seriesCount; ++s)
    {
        IntervalSeries &series = g_intervalSeries[s];
        const uint64_t total = series.count.load(std::memory_order_relaxed);
        if (total == 0)
            continue;
        const size_t count = static_cast<size_t>(
            total > kIntervalSampleCount ? kIntervalSampleCount : total);
        for (size_t i = 0; i < count; ++i)
            sorted[i] = series.samples[i].load(std::memory_order_relaxed);
        for (size_t i = 1; i < count; ++i)
        {
            const uint64_t value = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j - 1] > value)
            {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = value;
        }

        char line[512];
        int length = std::snprintf(
            line, sizeof(line),
            "extra,IntervalStats,%s,count,%llu,p50_us,%.3f,p90_us,%.3f,p95_us,%.3f,"
            "p99_us,%.3f,min_us,%.3f,max_us,%.3f\r\n",
            series.name ? series.name : "",
            static_cast<unsigned long long>(total),
            sorted[(count - 1) * 50 / 100] * scale,
            sorted[(count - 1) * 90 / 100] * scale,
            sorted[(count - 1) * 95 / 100] * scale,
            sorted[(count - 1) * 99 / 100] * scale,
            sorted[0] * scale, sorted[count - 1] * scale);
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));

        for (size_t bucket = 0; ok && bucket < kIntervalBucketCount; ++bucket)
        {
            const uint64_t hits = series.buckets[bucket].load(std::memory_order_relaxed);
            if (hits == 0)
                continue;
            length = std::snprintf(
                line, sizeof(line), "extra,IntervalHistogram,%s,ge_us,%llu,count,%llu\r\n",
                series.name ? series.name : "",
                static_cast<unsigned long long>(bucket * kIntervalBucketMicroseconds),
                static_cast<unsigned long long>(hits));
            ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
                 writeText(line, static_cast<size_t>(length));
        }
    }
}

uint64_t nowTicks()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
}

IntervalSeries *findIntervalSeries(const char *name)
{
    const size_t known = g_intervalSeriesCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < known; ++i)
    {
        if (g_intervalSeries[i].name == name ||
            (g_intervalSeries[i].name && std::strcmp(g_intervalSeries[i].name, name) == 0))
            return &g_intervalSeries[i];
    }

    std::lock_guard<std::mutex> lock(g_intervalSeriesLock);
    const size_t used = g_intervalSeriesCount.load(std::memory_order_relaxed);
    for (size_t i = 0; i < used; ++i)
    {
        if (g_intervalSeries[i].name && std::strcmp(g_intervalSeries[i].name, name) == 0)
            return &g_intervalSeries[i];
    }
    if (used == kIntervalSeriesCount)
        return nullptr;
    g_intervalSeries[used].name = name;
    g_intervalSeriesCount.store(used + 1, std::memory_order_release);
    return &g_intervalSeries[used];
}

void recordInterval(IntervalSeries &series, uint64_t interval)
{
    /* Wrap, so percentiles describe the last window rather than start-up. */
    const uint64_t index = series.count.fetch_add(1, std::memory_order_relaxed);
    series.samples[index % kIntervalSampleCount].store(interval, std::memory_order_relaxed);

    const uint64_t microseconds =
        interval * 1000000ULL / static_cast<uint64_t>(g_frequency.QuadPart);
    size_t bucket = static_cast<size_t>(microseconds / kIntervalBucketMicroseconds);
    if (bucket >= kIntervalBucketCount)
        bucket = kIntervalBucketCount - 1;
    series.buckets[bucket].fetch_add(1, std::memory_order_relaxed);
}

uint64_t hashKey(const char *domain, const char *name)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const char *p = domain; p && *p; ++p)
        hash = (hash ^ static_cast<unsigned char>(*p)) * 1099511628211ULL;
    hash = (hash ^ ':') * 1099511628211ULL;
    for (const char *p = name; p && *p; ++p)
        hash = (hash ^ static_cast<unsigned char>(*p)) * 1099511628211ULL;
    return hash;
}

void writeReport()
{
    if (!g_enabled)
        return;

    /* Serialise on the lock alone: the atomic_flag this replaces made a second
     * caller give up, which is how the report written at exit was dropped. */
    std::lock_guard<std::mutex> writeLock(g_writeLock);

    const char *path = std::getenv("LL_PERF_PROFILE_FILE");
    if (!path || !*path)
        path = "pacloader-perf.csv";

    // Do not use stdio, std::string, or std::vector here. This function can run
    // concurrently with the guest/GL threads, and those paths may be backed by
    // the loader's allocator. Direct Win32 I/O keeps report generation outside
    // that allocator bridge.

    /* Build beside the target and rename on success, so a run that ends
     * mid-write cannot leave a truncated report. */
    char temporaryPath[1024];
    const int temporaryLength =
        std::snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", path);
    if (temporaryLength <= 0 || static_cast<size_t>(temporaryLength) >= sizeof(temporaryPath))
        return;

    HANDLE file = CreateFileA(temporaryPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    auto writeText = [file](const char *text, size_t length) {
        while (length > 0)
        {
            DWORD written = 0;
            if (!WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr) ||
                written == 0)
                return false;
            text += written;
            length -= written;
        }
        return true;
    };

    static constexpr char header[] =
        "thread_id,domain,function,calls,total_us,average_us,max_us,bytes\r\n";
    bool ok = writeText(header, sizeof(header) - 1);

    std::lock_guard<std::mutex> lock(g_threadsLock);
    for (size_t threadIndex = 0; ok && threadIndex < g_threadCount; ++threadIndex)
    {
        const ThreadData *thread = g_threads[threadIndex];
        // Entries are placed by hash, so they are sparse; `used` is only a
        // count and cannot be used as the upper bound for this scan.
        for (size_t i = 0; ok && i < kEntryCount; ++i)
        {
            const Entry &entry = thread->entries[i];
            if (!entry.name)
                continue;
            const uint64_t calls = entry.calls.load(std::memory_order_relaxed);
            const uint64_t ticks = entry.ticks.load(std::memory_order_relaxed);
            const uint64_t maxTicks = entry.maxTicks.load(std::memory_order_relaxed);
            const uint64_t bytes = entry.bytes.load(std::memory_order_relaxed);
            const double totalUs = static_cast<double>(ticks) * 1000000.0 /
                                   static_cast<double>(g_frequency.QuadPart);
            const double maxUs = static_cast<double>(maxTicks) * 1000000.0 /
                                 static_cast<double>(g_frequency.QuadPart);
            const double averageUs = calls ? totalUs / static_cast<double>(calls) : 0.0;

            char line[1024];
            const int length = std::snprintf(
                line, sizeof(line), "%lu,%s,%s,%llu,%.3f,%.3f,%.3f,%llu\r\n",
                static_cast<unsigned long>(thread->threadId),
                entry.domain ? entry.domain : "", entry.name ? entry.name : "",
                static_cast<unsigned long long>(calls), totalUs, averageUs, maxUs,
                static_cast<unsigned long long>(bytes));
            if (length <= 0 || static_cast<size_t>(length) >= sizeof(line))
            {
                ok = false;
                break;
            }
            ok = writeText(line, static_cast<size_t>(length));
        }
    }

    if (ok)
    {
        const uint64_t skips = g_extraStats.cacheSkips.load(std::memory_order_relaxed);
        const uint64_t samples = g_extraStats.frameSamples.load(std::memory_order_relaxed);
        char line[256];
        const int length = std::snprintf(line, sizeof(line),
                                         "extra,GLCache,skipped_calls,%llu,,,,\r\n",
                                         static_cast<unsigned long long>(skips));
        ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
             writeText(line, static_cast<size_t>(length));
        if (ok)
        {
            const int sampleLength = std::snprintf(
                line, sizeof(line), "extra,FrameStats,samples,%llu,,,,\r\n",
                static_cast<unsigned long long>(samples));
            ok = sampleLength > 0 && static_cast<size_t>(sampleLength) < sizeof(line) &&
                 writeText(line, static_cast<size_t>(sampleLength));
        }
        if (ok && samples > 0)
        {
            uint64_t sorted[kFrameSampleCount];
            const size_t count = static_cast<size_t>(samples > kFrameSampleCount
                                                          ? kFrameSampleCount
                                                          : samples);
            for (size_t i = 0; i < count; ++i)
                sorted[i] = g_extraStats.frameTicks[i].load(std::memory_order_relaxed);
            for (size_t i = 1; i < count; ++i)
            {
                uint64_t value = sorted[i];
                size_t j = i;
                while (j > 0 && sorted[j - 1] > value)
                {
                    sorted[j] = sorted[j - 1];
                    --j;
                }
                sorted[j] = value;
            }
            const size_t p50 = (count - 1) * 50 / 100;
            const size_t p95 = (count - 1) * 95 / 100;
            const size_t p99 = (count - 1) * 99 / 100;
            const double scale = 1000000.0 / static_cast<double>(g_frequency.QuadPart);
            const int statsLength = std::snprintf(
                line, sizeof(line), "extra,FrameStats,p50_p95_p99_us,%llu/%.3f/%.3f/%.3f,,,,\r\n",
                static_cast<unsigned long long>(count), sorted[p50] * scale,
                sorted[p95] * scale, sorted[p99] * scale);
            ok = statsLength > 0 && static_cast<size_t>(statsLength) < sizeof(line) &&
                 writeText(line, static_cast<size_t>(statsLength));
        }
    }

    if (ok)
    {
        for (size_t i = 0; i < kTextureQueryStatCount; ++i)
        {
            const uint64_t calls = g_textureQueryStats[i].calls.load(std::memory_order_relaxed);
            if (calls == 0)
                continue;
            char line[256];
            const uint64_t hits = g_textureQueryStats[i].hits.load(std::memory_order_relaxed);
            const uint32_t pname = g_textureQueryStats[i].pname.load(std::memory_order_relaxed);
            const int length = std::snprintf(
                line, sizeof(line), "extra,TexQueryStats,pname,0x%08llx,calls,%llu,hits,%llu,misses,%llu\r\n",
                static_cast<unsigned long long>(pname),
                static_cast<unsigned long long>(calls),
                static_cast<unsigned long long>(hits),
                static_cast<unsigned long long>(calls - hits));
            ok = length > 0 && static_cast<size_t>(length) < sizeof(line) &&
                 writeText(line, static_cast<size_t>(length));
            if (!ok)
                break;
        }
    }

    /* Interval series first: it is the only pacing measurement here, and writing
     * it last meant any earlier failure took it along. */
    if (ok)
        writeIntervalReport(writeText, ok);
    if (ok)
        writeGpuReport(writeText, ok);
    if (ok)
        writeBufferTraceReport(writeText, ok);
    if (ok)
        writeFrameSpikeReport(writeText, ok);
    if (ok)
        writePresentSpikeReport(writeText, ok);

    CloseHandle(file);
    if (ok)
        MoveFileExA(temporaryPath, path, MOVEFILE_REPLACE_EXISTING);
    else
        DeleteFileA(temporaryPath);
}

DWORD WINAPI flushThreadProc(void *)
{
    while (!g_flushStop.load(std::memory_order_acquire))
    {
        Sleep(2000);
        if (!g_flushStop.load(std::memory_order_acquire))
            writeReport();
    }
    return 0;
}

void shutdownProfiler()
{
    g_flushStop.store(true, std::memory_order_release);
    if (g_flushThread)
    {
        WaitForSingleObject(g_flushThread, 3000);
        CloseHandle(g_flushThread);
        g_flushThread = nullptr;
    }
    writeReport();
}

void initialize()
{
    QueryPerformanceFrequency(&g_frequency);
    g_frameSpikeThresholdTicks = static_cast<uint64_t>(
        (static_cast<long double>(g_frequency.QuadPart) * 16667.0L) / 1000000.0L);
    const char *spikeSetting = std::getenv("LL_PERF_SPIKE_US");
    if (spikeSetting && *spikeSetting)
    {
        const long value = std::strtol(spikeSetting, nullptr, 10);
        if (value > 0)
            g_frameSpikeThresholdTicks = static_cast<uint64_t>(
                (static_cast<long double>(g_frequency.QuadPart) * value) / 1000000.0L);
    }
    const char *setting = std::getenv("LL_PERF_PROFILE");
    g_enabled = setting && *setting && std::strcmp(setting, "0") != 0;
    const char *bufferTrace = std::getenv("LL_GL_BUFFER_TRACE");
    g_bufferTraceEnabled = g_enabled && bufferTrace &&
                           std::strcmp(bufferTrace, "1") == 0;
    const char *flushSetting = std::getenv("LL_PERF_PROFILE_FLUSH");
    g_backgroundFlushEnabled = !flushSetting || std::strcmp(flushSetting, "0") != 0;
    g_enabledState.store(g_enabled ? 1 : 0, std::memory_order_release);
    if (g_enabled)
    {
        g_tlsIndex = TlsAlloc();
        std::atexit(shutdownProfiler);
        if (g_backgroundFlushEnabled)
            g_flushThread = CreateThread(nullptr, 0, flushThreadProc, nullptr, 0, nullptr);
    }
}

/* Parked in TLS once the table is full, so an over-cap thread stops asking:
 * without it every profiled call from one allocated and leaked ~49 KB. */
ThreadData *const kThreadNotTracked = reinterpret_cast<ThreadData *>(-1);

ThreadData *threadData()
{
    if (g_tlsIndex == TLS_OUT_OF_INDEXES)
        return nullptr;

    if (ThreadData *existing = static_cast<ThreadData *>(TlsGetValue(g_tlsIndex)))
        return existing == kThreadNotTracked ? nullptr : existing;

    /* Claim the slot before allocating, so a full table costs nothing. */
    {
        std::lock_guard<std::mutex> lock(g_threadsLock);
        if (g_threadCount >= kMaxThreads)
        {
            TlsSetValue(g_tlsIndex, kThreadNotTracked);
            return nullptr;
        }
    }

    void *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadData));
    if (!memory)
        return nullptr;

    ThreadData *created = new (memory) ThreadData{};
    created->threadId = GetCurrentThreadId();

    std::lock_guard<std::mutex> lock(g_threadsLock);
    if (g_threadCount >= kMaxThreads)
    {
        HeapFree(GetProcessHeap(), 0, memory);
        TlsSetValue(g_tlsIndex, kThreadNotTracked);
        return nullptr;
    }
    g_threads[g_threadCount++] = created;
    TlsSetValue(g_tlsIndex, created);
    return created;
}

Entry *findEntry(ThreadData *thread, const char *domain, const char *name)
{
    const uint64_t hash = hashKey(domain, name);
    size_t index = static_cast<size_t>(hash % kEntryCount);
    for (size_t probe = 0; probe < kEntryCount; ++probe)
    {
        Entry &entry = thread->entries[index];
        if (!entry.name)
        {
            const size_t used = thread->used.load(std::memory_order_relaxed);
            if (used == kEntryCount)
                return nullptr;
            entry.domain = domain;
            entry.name = name;
            thread->used.store(used + 1, std::memory_order_release);
            return &entry;
        }
        if (std::strcmp(entry.domain, domain) == 0 && std::strcmp(entry.name, name) == 0)
            return &entry;
        index = (index + 1) % kEntryCount;
    }
    return nullptr;
}
}

extern "C" int PerfProfiler_IsEnabled(void)
{
    const int state = g_enabledState.load(std::memory_order_acquire);
    if (state >= 0)
        return state;
    std::call_once(g_initOnce, initialize);
    return g_enabledState.load(std::memory_order_acquire);
}

extern "C" uint64_t PerfProfiler_Begin(const char *domain, const char *name)
{
    if (!PerfProfiler_IsEnabled() ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return 0;
    return nowTicks();
}

extern "C" void PerfProfiler_End(const char *domain, const char *name,
                                  uint64_t start, uint64_t bytes)
{
    if (!start || !PerfProfiler_IsEnabled())
        return;

    ThreadData *thread = threadData();
    if (!thread)
        return;
    Entry *entry = findEntry(thread, domain, name);
    if (!entry)
        return;

    const uint64_t elapsed = nowTicks() - start;
    addFrameEvent(domain, name, elapsed);
    if (domain && name && std::strcmp(domain, "Frame") == 0 &&
        std::strcmp(name, "frameTiming") == 0)
        PerfProfiler_FrameSample(elapsed);
    entry->calls.fetch_add(1, std::memory_order_relaxed);
    entry->ticks.fetch_add(elapsed, std::memory_order_relaxed);
    entry->bytes.fetch_add(bytes, std::memory_order_relaxed);

    uint64_t previousMax = entry->maxTicks.load(std::memory_order_relaxed);
    while (previousMax < elapsed &&
           !entry->maxTicks.compare_exchange_weak(previousMax, elapsed,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed))
    {
    }

}

extern "C" void PerfProfiler_Flush(void)
{
    if (PerfProfiler_IsEnabled())
        writeReport();
}

extern "C" void PerfProfiler_MarkRuntimeReady(void)
{
    if (PerfProfiler_IsEnabled())
        g_runtimeReady.store(true, std::memory_order_release);
}

extern "C" void PerfProfiler_CacheSkip(const char *)
{
    if (PerfProfiler_IsEnabled() && g_runtimeReady.load(std::memory_order_acquire))
        g_extraStats.cacheSkips.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void PerfProfiler_TextureQuery(uint32_t pname, int cacheHit)
{
    if (!PerfProfiler_IsEnabled() || !g_runtimeReady.load(std::memory_order_acquire))
        return;
    TextureQueryStat &stat = g_textureQueryStats[pname % kTextureQueryStatCount];
    uint32_t unset = 0;
    stat.pname.compare_exchange_strong(unset, pname, std::memory_order_relaxed,
                                       std::memory_order_relaxed);
    stat.calls.fetch_add(1, std::memory_order_relaxed);
    if (cacheHit)
        stat.hits.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void PerfProfiler_GpuFrameSample(uint64_t busyNanoseconds,
                                             int64_t backlogNanoseconds)
{
    if (!PerfProfiler_IsEnabled() || !g_runtimeReady.load(std::memory_order_acquire))
        return;
    /* Wrap, so the percentiles describe the whole run. */
    const uint64_t index = g_gpuStats.samples.fetch_add(1, std::memory_order_relaxed);
    const size_t slot = static_cast<size_t>(index % kGpuSampleCount);
    g_gpuStats.busyNs[slot].store(busyNanoseconds, std::memory_order_relaxed);
    g_gpuStats.backlogNs[slot].store(backlogNanoseconds, std::memory_order_relaxed);
}

extern "C" void PerfProfiler_FrameSample(uint64_t elapsedTicks)
{
    if (!PerfProfiler_IsEnabled() || !g_runtimeReady.load(std::memory_order_acquire))
        return;
    /* Wrap: keeping only the first samples made this describe start-up, not play. */
    const uint64_t index = g_extraStats.frameSamples.fetch_add(1, std::memory_order_relaxed);
    g_extraStats.frameTicks[index % kFrameSampleCount].store(elapsedTicks,
                                                             std::memory_order_relaxed);
}

void recordFrameBoundary(uint64_t now)
{
    const uint64_t start = g_frameBoundaryStart.load(std::memory_order_acquire);
    const uint64_t elapsed = now > start ? now - start : 0;
    /* Count every frame: the sequence used to be a spike counter, so a spike
     * could not be placed in the run. */
    const uint64_t ordinal = g_frameSequence.fetch_add(1, std::memory_order_relaxed);
    if (elapsed >= g_frameSpikeThresholdTicks)
    {
        FrameSpike spike{};
        spike.sequence = ordinal;
        spike.totalTicks = elapsed;
        spike.glTicks = g_frameAccumulator.glTicks.exchange(0, std::memory_order_relaxed);
        spike.pthreadTicks = g_frameAccumulator.pthreadTicks.exchange(0, std::memory_order_relaxed);
        spike.presentTicks = g_frameAccumulator.presentTicks.exchange(0, std::memory_order_relaxed);
        spike.glxTicks = g_frameAccumulator.glxTicks.exchange(0, std::memory_order_relaxed);
        spike.otherTicks = g_frameAccumulator.otherTicks.exchange(0, std::memory_order_relaxed);
        spike.glCalls = g_frameAccumulator.glCalls.exchange(0, std::memory_order_relaxed);
        spike.pthreadCalls = g_frameAccumulator.pthreadCalls.exchange(0, std::memory_order_relaxed);
        spike.presentCalls = g_frameAccumulator.presentCalls.exchange(0, std::memory_order_relaxed);
        spike.glxCalls = g_frameAccumulator.glxCalls.exchange(0, std::memory_order_relaxed);
        spike.worstTicks = g_frameAccumulator.worstTicks.exchange(0, std::memory_order_relaxed);
        spike.worstDomain = g_frameAccumulator.worstDomain.exchange(nullptr, std::memory_order_relaxed);
        spike.worstName = g_frameAccumulator.worstName.exchange(nullptr, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(g_frameSpikeLock);
        g_frameSpikes[g_frameSpikeCount++ % kFrameSpikeCount] = spike;
    }
    else
    {
        resetFrameAccumulator();
    }
}

extern "C" void PerfProfiler_FrameBoundaryStart(void)
{
    if (!PerfProfiler_IsEnabled() ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return;

    bool expected = false;
    if (g_frameBoundaryActive.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_relaxed))
    {
        g_frameBoundaryStart.store(nowTicks(), std::memory_order_release);
        resetFrameAccumulator();
    }
}

extern "C" void PerfProfiler_FrameBoundaryEnd(void)
{
    if (!PerfProfiler_IsEnabled() ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return;

    bool expected = true;
    if (g_frameBoundaryActive.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel,
            std::memory_order_relaxed))
        recordFrameBoundary(nowTicks());
}

extern "C" void PerfProfiler_FrameBoundaryEndAndStart(void)
{
    PerfProfiler_FrameBoundaryEnd();
    PerfProfiler_FrameBoundaryStart();
}

extern "C" uint64_t PerfProfiler_NowTicks(void)
{
    if (!PerfProfiler_IsEnabled())
        return 0;
    return nowTicks();
}

extern "C" void PerfProfiler_PresentTransaction(const char *source,
                                                  uint64_t totalTicks,
                                                  uint64_t eventTicks,
                                                  uint64_t pacingTicks,
                                                  uint64_t swapTicks)
{
    if (!PerfProfiler_IsEnabled() ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return;

    /* The interval is what the screen shows; `gap` is the part outside this
     * call, which separates a guest stall from a loader one. */
    const uint64_t now = nowTicks();
    const uint64_t previous = g_lastPresentEnd.exchange(now, std::memory_order_relaxed);
    if (previous == 0 || now <= previous)
        return;

    const uint64_t interval = now - previous;
    IntervalSeries *series = findIntervalSeries("present");
    if (series)
        recordInterval(*series, interval);

    /* Counted for every present, so the ordinal places a spike in the run; it
     * used to be the spike index, which carried no timing information. */
    const uint64_t ordinal = g_presentSequence.fetch_add(1, std::memory_order_relaxed);

    if (interval < g_frameSpikeThresholdTicks)
        return;

    PresentSpike spike{};
    if (source)
        std::strncpy(spike.source, source, sizeof(spike.source) - 1);
    spike.sequence = ordinal;
    spike.totalTicks = totalTicks;
    spike.eventTicks = eventTicks;
    spike.pacingTicks = pacingTicks;
    spike.swapTicks = swapTicks;
    spike.intervalTicks = interval;
    spike.gapTicks = interval > totalTicks ? interval - totalTicks : 0;

    std::lock_guard<std::mutex> lock(g_presentSpikeLock);
    g_presentSpikes[g_presentSpikeCount++ % kPresentSpikeCount] = spike;
}

/* A duration rather than a gap between marks, through the same wrapping series,
 * so a cost that averages well can still be shown to be bimodal. */
extern "C" void PerfProfiler_DurationMark(const char *name, uint64_t ticks)
{
    if (!PerfProfiler_IsEnabled() || !name || ticks == 0 ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return;
    if (IntervalSeries *series = findIntervalSeries(name))
        recordInterval(*series, ticks);
}

extern "C" void PerfProfiler_IntervalMark(const char *name)
{
    if (!PerfProfiler_IsEnabled() || !name ||
        !g_runtimeReady.load(std::memory_order_acquire))
        return;

    IntervalSeries *series = findIntervalSeries(name);
    if (!series)
        return;

    const uint64_t now = nowTicks();
    const uint64_t previous = series->last.exchange(now, std::memory_order_relaxed);
    if (previous == 0 || now <= previous)
        return;
    recordInterval(*series, now - previous);
}

extern "C" void PerfProfiler_GLBufferEvent(const char *operation, uintptr_t caller,
                                            uintptr_t context, uint32_t target,
                                            uint32_t usage, uint64_t size,
                                            uint32_t buffer)
{
    if (!g_bufferTraceEnabled || !g_runtimeReady.load(std::memory_order_acquire))
        return;

    BufferOperation bufferOperation;
    if (operation && std::strcmp(operation, "gen") == 0)
        bufferOperation = BufferOperation::Gen;
    else if (operation && std::strcmp(operation, "data") == 0)
        bufferOperation = BufferOperation::Data;
    else if (operation && std::strcmp(operation, "delete") == 0)
        bufferOperation = BufferOperation::Delete;
    else if (operation && std::strcmp(operation, "bind") == 0)
        bufferOperation = BufferOperation::Bind;
    else
        return;

    std::lock_guard<std::mutex> lock(g_bufferTraceLock);

    BufferSiteEntry *site = findBufferSite(bufferOperation, caller, context,
                                           target, usage,
                                           bufferOperation == BufferOperation::Data ? size : 0);
    if (site)
    {
        ++site->calls;
        if (bufferOperation == BufferOperation::Data)
            site->bytes += size;
    }

    if (buffer == 0)
        return;

    BufferObjectEntry *object = findBufferObject(context, buffer, true);
    if (!object)
        return;

    if (bufferOperation == BufferOperation::Gen)
    {
        if (object->everDeleted)
        {
            if (BufferPatternEntry *pattern = findBufferPattern(
                    object->context, object->target, object->usage, object->size))
                ++pattern->reusedNames;
        }
        object->active = true;
        object->everDeleted = false;
        object->target = 0;
        object->usage = 0;
        object->size = 0;
        object->dataCalls = 0;
        object->bindCalls = 0;
        return;
    }

    if (bufferOperation == BufferOperation::Data)
    {
        object->active = true;
        object->target = target;
        object->usage = usage;
        object->size = size;
        ++object->dataCalls;

        if (BufferPatternEntry *pattern = findBufferPattern(context, target, usage, size))
            ++pattern->dataCalls;
        return;
    }

    if (bufferOperation == BufferOperation::Bind)
    {
        object->active = true;
        ++object->bindCalls;
        return;
    }

    if (object->active && object->dataCalls > 0)
    {
        if (BufferPatternEntry *pattern = findBufferPattern(
                object->context, object->target, object->usage, object->size))
        {
            ++pattern->deletedObjects;
            ++pattern->objectsWithData;
            pattern->bindCalls += object->bindCalls;
            if (object->bindCalls > 0)
                ++pattern->objectsWithBinds;
        }
    }
    object->active = false;
    object->everDeleted = true;
}

#else

extern "C" int PerfProfiler_IsEnabled(void) { return 0; }
extern "C" uint64_t PerfProfiler_Begin(const char *, const char *) { return 0; }
extern "C" void PerfProfiler_End(const char *, const char *, uint64_t, uint64_t) {}
extern "C" void PerfProfiler_Flush(void) {}
extern "C" void PerfProfiler_MarkRuntimeReady(void) {}
extern "C" void PerfProfiler_CacheSkip(const char *) {}
extern "C" void PerfProfiler_TextureQuery(uint32_t, int) {}
extern "C" void PerfProfiler_FrameSample(uint64_t) {}
extern "C" void PerfProfiler_GpuFrameSample(uint64_t, int64_t) {}
extern "C" void PerfProfiler_FrameBoundaryEndAndStart(void) {}
extern "C" void PerfProfiler_FrameBoundaryStart(void) {}
extern "C" void PerfProfiler_FrameBoundaryEnd(void) {}
extern "C" uint64_t PerfProfiler_NowTicks(void) { return 0; }
extern "C" void PerfProfiler_PresentTransaction(const char *, uint64_t, uint64_t,
                                                 uint64_t, uint64_t) {}
extern "C" void PerfProfiler_IntervalMark(const char *) {}
extern "C" void PerfProfiler_DurationMark(const char *, uint64_t) {}
extern "C" void PerfProfiler_GLBufferEvent(const char *, uintptr_t, uintptr_t,
                                            uint32_t, uint32_t, uint64_t, uint32_t) {}

#endif
