#include "threadWatchdog.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../log/log.h"

namespace
{

/* How much of each thread's stack is copied out while it is suspended.  Deep
 * enough to cross the bridge frames and reach guest callers, small enough that
 * the suspension stays sub-millisecond. */
constexpr SIZE_T kStackScanBytes = 16 * 1024;

/* Upper bound on reported return addresses per thread. */
constexpr int kMaxFramesReported = 20;

struct CodeRegion
{
    uintptr_t begin;
    uintptr_t end;
    bool host;          /* Backed by a loaded Windows module. */
    std::string name;   /* Module basename, empty for guest mappings. */
};

struct ThreadSample
{
    DWORD tid;
    bool captured;
    DWORD eip;
    DWORD esp;
    DWORD ebp;
    std::vector<uint32_t> stack;
};

std::string moduleBaseName(HMODULE module)
{
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(module, path, sizeof(path)) == 0)
        return std::string();

    const char *slash = std::strrchr(path, '\\');
    return std::string(slash ? slash + 1 : path);
}

/* Snapshot every committed executable region, tagging the ones that belong to a
 * loaded Windows module.  Anything executable that is not part of a module is
 * memory the ELF loader mapped, i.e. guest code. */
std::vector<CodeRegion> collectCodeRegions()
{
    std::vector<CodeRegion> regions;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);

    uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t limit = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);

    while (address < limit)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t next = base + mbi.RegionSize;

        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & executable) != 0)
        {
            CodeRegion region{};
            region.begin = base;
            region.end = next;

            HMODULE module = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(base), &module) &&
                module != nullptr)
            {
                region.host = true;
                region.name = moduleBaseName(module);
            }

            regions.push_back(std::move(region));
        }

        if (next <= address)
            break;
        address = next;
    }

    std::sort(regions.begin(), regions.end(),
              [](const CodeRegion &a, const CodeRegion &b) { return a.begin < b.begin; });
    return regions;
}

const CodeRegion *findRegion(const std::vector<CodeRegion> &regions, uintptr_t address)
{
    auto it = std::upper_bound(regions.begin(), regions.end(), address,
                               [](uintptr_t value, const CodeRegion &region)
                               { return value < region.begin; });
    if (it == regions.begin())
        return nullptr;
    --it;
    return (address >= it->begin && address < it->end) ? &*it : nullptr;
}

void describeAddress(const std::vector<CodeRegion> &regions, uint32_t address,
                     char *out, size_t outSize)
{
    const CodeRegion *region = findRegion(regions, address);
    if (!region)
    {
        std::snprintf(out, outSize, "0x%08lx <not executable>",
                      static_cast<unsigned long>(address));
        return;
    }

    if (region->host)
    {
        std::snprintf(out, outSize, "0x%08lx %s+0x%lx",
                      static_cast<unsigned long>(address),
                      region->name.empty() ? "?" : region->name.c_str(),
                      static_cast<unsigned long>(address - region->begin));
        return;
    }

    std::snprintf(out, outSize, "0x%08lx GUEST", static_cast<unsigned long>(address));
}

/* Suspend one thread just long enough to copy its register state and a slice of
 * its stack.  Nothing is logged while the thread is stopped: taking the logger
 * lock against a suspended owner would hang the whole process. */
bool sampleThread(DWORD tid, ThreadSample &sample)
{
    sample.tid = tid;
    sample.captured = false;

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, tid);
    if (!thread)
        return false;

    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return false;
    }

    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(thread, &context))
    {
        sample.eip = context.Eip;
        sample.esp = context.Esp;
        sample.ebp = context.Ebp;

        /* Clamp the copy to the committed part of the stack region so a short
         * stack does not turn into a spurious access violation. */
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T available = 0;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(context.Esp), &mbi, sizeof(mbi)) == sizeof(mbi) &&
            mbi.State == MEM_COMMIT)
        {
            const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            available = static_cast<SIZE_T>(regionEnd - context.Esp);
        }

        const SIZE_T wanted = std::min<SIZE_T>(kStackScanBytes, available);
        if (wanted >= sizeof(uint32_t))
        {
            sample.stack.resize(wanted / sizeof(uint32_t));
            SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(context.Esp),
                                   sample.stack.data(), sample.stack.size() * sizeof(uint32_t),
                                   &read))
                sample.stack.clear();
            else
                sample.stack.resize(read / sizeof(uint32_t));
        }

        sample.captured = true;
    }

    ResumeThread(thread);
    CloseHandle(thread);
    return sample.captured;
}

void reportSample(const std::vector<CodeRegion> &regions, const ThreadSample &sample)
{
    char where[160]{};
    describeAddress(regions, sample.eip, where, sizeof(where));
    log_info("watchdog: tid=%lu eip=%s esp=0x%08lx ebp=0x%08lx",
             static_cast<unsigned long>(sample.tid), where,
             static_cast<unsigned long>(sample.esp),
             static_cast<unsigned long>(sample.ebp));

    /* Scan for guest return addresses rather than walking EBP: the guest is
     * compiled without frame pointers, so the chain is not reliable. */
    int reported = 0;
    uint32_t previous = 0;
    for (size_t i = 0; i < sample.stack.size() && reported < kMaxFramesReported; ++i)
    {
        const uint32_t candidate = sample.stack[i];
        if (candidate == previous)
            continue;

        const CodeRegion *region = findRegion(regions, candidate);
        if (!region || region->host)
            continue;

        previous = candidate;
        log_info("watchdog:   tid=%lu guest frame [%d] 0x%08lx (stack+0x%lx)",
                 static_cast<unsigned long>(sample.tid), reported,
                 static_cast<unsigned long>(candidate),
                 static_cast<unsigned long>(i * sizeof(uint32_t)));
        ++reported;
    }

    if (reported == 0)
        log_info("watchdog:   tid=%lu no guest frames on stack",
                 static_cast<unsigned long>(sample.tid));
}

void dumpAllThreads(int generation)
{
    const DWORD ownPid = GetCurrentProcessId();
    const DWORD ownTid = GetCurrentThreadId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        log_warn("watchdog: cannot snapshot threads (error %lu)",
                 static_cast<unsigned long>(GetLastError()));
        return;
    }

    std::vector<DWORD> threads;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID == ownPid && entry.th32ThreadID != ownTid)
                threads.push_back(entry.th32ThreadID);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::vector<ThreadSample> samples;
    samples.reserve(threads.size());
    for (const DWORD tid : threads)
    {
        ThreadSample sample{};
        if (sampleThread(tid, sample))
            samples.push_back(std::move(sample));
    }

    /* Region collection is deliberately done after every thread has been
     * resumed; it calls into the loader and must not run under suspension. */
    const std::vector<CodeRegion> regions = collectCodeRegions();

    log_info("watchdog: ===== sample #%d: %zu threads =====", generation, samples.size());
    for (const ThreadSample &sample : samples)
        reportSample(regions, sample);
    log_info("watchdog: ===== end of sample #%d =====", generation);
}

DWORD WINAPI watchdogMain(LPVOID parameter)
{
    const unsigned period = static_cast<unsigned>(reinterpret_cast<uintptr_t>(parameter));

    for (int generation = 1;; ++generation)
    {
        Sleep(period * 1000u);
        dumpAllThreads(generation);
    }
}

} // namespace

namespace ThreadWatchdog
{

void Start()
{
    const char *setting = std::getenv("LL_WATCHDOG_SEC");
    if (!setting || !*setting)
        return;

    const long period = std::strtol(setting, nullptr, 10);
    if (period <= 0)
        return;

    HANDLE thread = CreateThread(nullptr, 0, watchdogMain,
                                 reinterpret_cast<LPVOID>(static_cast<uintptr_t>(period)),
                                 0, nullptr);
    if (!thread)
    {
        log_warn("watchdog: failed to start (error %lu)",
                 static_cast<unsigned long>(GetLastError()));
        return;
    }
    CloseHandle(thread);

    log_info("watchdog: sampling every %ld second(s)", period);
}

} // namespace ThreadWatchdog
