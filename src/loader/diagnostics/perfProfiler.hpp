#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns zero when LL_PERF_PROFILE is unset or set to 0. */
int PerfProfiler_IsEnabled(void);

/* C entry points for C translation units. The returned value is opaque. */
uint64_t PerfProfiler_Begin(const char *domain, const char *name);
void PerfProfiler_End(const char *domain, const char *name,
                      uint64_t start, uint64_t bytes);
void PerfProfiler_Flush(void);
void PerfProfiler_MarkRuntimeReady(void);
void PerfProfiler_CacheSkip(const char *name);
void PerfProfiler_TextureQuery(uint32_t pname, int cacheHit);
void PerfProfiler_FrameSample(uint64_t elapsedTicks);
void PerfProfiler_FrameBoundaryEndAndStart(void);
void PerfProfiler_FrameBoundaryStart(void);
void PerfProfiler_FrameBoundaryEnd(void);
uint64_t PerfProfiler_NowTicks(void);
void PerfProfiler_PresentTransaction(const char *source, uint64_t totalTicks,
                                     uint64_t eventTicks, uint64_t pacingTicks,
                                     uint64_t swapTicks);
/* Records the interval between successive calls, one series per name. Used to
 * measure the pacing the display actually sees rather than the work per frame. */
void PerfProfiler_IntervalMark(const char *name);
void PerfProfiler_GLBufferEvent(const char *operation, uintptr_t caller,
                                uintptr_t context, uint32_t target, uint32_t usage,
                                uint64_t size, uint32_t buffer);

#ifdef __cplusplus
}

class PerfProfilerScope
{
public:
    PerfProfilerScope(const char *domain, const char *name)
        : domain_(domain), name_(name), start_(PerfProfiler_Begin(domain, name)), bytes_(0)
    {
    }

    ~PerfProfilerScope()
    {
        PerfProfiler_End(domain_, name_, start_, bytes_);
    }

    void AddBytes(uint64_t bytes) { bytes_ += bytes; }

private:
    const char *domain_;
    const char *name_;
    uint64_t start_;
    uint64_t bytes_;
};

#define PERF_PROFILE_SCOPE(domain) \
    PerfProfilerScope perfProfilerScope(domain, __func__)
#endif
