// ============================================================
// pthread_mapper.cpp - Object Mapper 
// ============================================================

#include "pthreadInternal.hpp"
#include "../../diagnostics/perfProfiler.hpp"
#include <unordered_map>

// ============================================================
// Static members
// ============================================================

SRWLOCK PthreadMapper::s_lock = SRWLOCK_INIT;
volatile bool PthreadMapper::s_initialized = false;

namespace {

/* Shard pthread object tables by guest pointer to reduce lookup contention. */
constexpr size_t kShardCount = 64;

/* Cache repeated pthread object lookups per thread. A direct-mapped four-entry
 * cache was too easy to thrash because WMMT touches several mutexes in a tight
 * loop. Keep the cache small, but use 16 sets with four ways so unrelated
 * guest addresses do not evict each other immediately. */
constexpr size_t kCacheSets = 16;
constexpr size_t kCacheWays = 4;
constexpr size_t kCacheEntries = kCacheSets * kCacheWays;
volatile LONG g_tableGeneration = 1;

struct MutexFastEntry
{
    LONG generation = 0;
    void *key = nullptr;
    PthreadMutexInternal *value = nullptr;
};

/* The mutex path dominates the mapper traffic. Keep a tiny last-use cache in
 * front of the generic table cache; it avoids the template/cache bookkeeping
 * for the common lock/unlock pair on the same guest mutex. */
thread_local MutexFastEntry g_mutexFastCache[4]{};

static PthreadMutexInternal *fastMutexLookup(void *key)
{
    const size_t slot = (reinterpret_cast<uintptr_t>(key) >> 4) & 3u;
    MutexFastEntry &entry = g_mutexFastCache[slot];
    return entry.generation == g_tableGeneration && entry.key == key ? entry.value : nullptr;
}

static void rememberFastMutex(void *key, PthreadMutexInternal *value)
{
    const size_t slot = (reinterpret_cast<uintptr_t>(key) >> 4) & 3u;
    g_mutexFastCache[slot] = {g_tableGeneration, key, value};
}

template <typename T>
struct ShardedTable {
    struct Shard {
        SRWLOCK lock = SRWLOCK_INIT;
        std::unordered_map<void*, T*> map;
    };

    Shard shards[kShardCount];

    struct Cache {
        LONG generation;
        void* keys[kCacheEntries];
        T* values[kCacheEntries];
        unsigned char nextWay[kCacheSets];
    };

    static Cache& threadCache() {
        static thread_local Cache cache{};
        return cache;
    }

    static size_t cacheSlot(void* key) {
        return ((reinterpret_cast<uintptr_t>(key) >> 4) % kCacheSets) * kCacheWays;
    }

    T* cached(void* key) {
        Cache& cache = threadCache();
        const LONG generation = g_tableGeneration;
        if (cache.generation != generation) {
            cache = Cache{};
            cache.generation = generation;
            return nullptr;
        }
        const size_t first = cacheSlot(key);
        for (size_t way = 0; way < kCacheWays; ++way)
        {
            const size_t slot = first + way;
            if (cache.keys[slot] == key)
                return cache.values[slot];
        }
        return nullptr;
    }

    void remember(void* key, T* value) {
        Cache& cache = threadCache();
        const size_t first = cacheSlot(key);
        const size_t way = cache.nextWay[first / kCacheWays]++ % kCacheWays;
        const size_t slot = first + way;
        cache.keys[slot] = key;
        cache.values[slot] = value;
    }

    Shard& shardFor(void* key) {
        /* Ignore pointer alignment bits when selecting a shard. */
        const uintptr_t value = reinterpret_cast<uintptr_t>(key);
        return shards[(value >> 4) % kShardCount];
    }

    template <typename Factory>
    T* GetOrCreate(void* key, int magic, Factory make) {
        if (T* hit = cached(key)) {
            return hit;
        }

        Shard& shard = shardFor(key);

        AcquireSRWLockShared(&shard.lock);
        auto it = shard.map.find(key);
        if (it != shard.map.end() && it->second->initialized == magic) {
            T* existing = it->second;
            ReleaseSRWLockShared(&shard.lock);
            remember(key, existing);
            return existing;
        }
        ReleaseSRWLockShared(&shard.lock);

        AcquireSRWLockExclusive(&shard.lock);
        // Recheck after acquiring the shard lock.
        it = shard.map.find(key);
        if (it != shard.map.end() && it->second->initialized == magic) {
            T* existing = it->second;
            ReleaseSRWLockExclusive(&shard.lock);
            remember(key, existing);
            return existing;
        }

        T* created = make();
        shard.map[key] = created;
        ReleaseSRWLockExclusive(&shard.lock);
        remember(key, created);
        return created;
    }

    T* Find(void* key, int magic) {
        if (T* hit = cached(key)) {
            return hit;
        }

        Shard& shard = shardFor(key);

        AcquireSRWLockShared(&shard.lock);
        auto it = shard.map.find(key);
        T* result = nullptr;
        if (it != shard.map.end() && it->second->initialized == magic) {
            result = it->second;
        }
        ReleaseSRWLockShared(&shard.lock);
        if (result) {
            remember(key, result);
        }
        return result;
    }

    template <typename Release>
    void Destroy(void* key, Release release) {
        InterlockedIncrement(&g_tableGeneration);

        Shard& shard = shardFor(key);

        AcquireSRWLockExclusive(&shard.lock);
        auto it = shard.map.find(key);
        if (it != shard.map.end()) {
            if (it->second) {
                release(it->second);
                it->second->initialized = 0;
                delete it->second;
            }
            shard.map.erase(it);
        }
        ReleaseSRWLockExclusive(&shard.lock);
    }

    template <typename Release>
    void Clear(Release release) {
        InterlockedIncrement(&g_tableGeneration);

        for (Shard& shard : shards) {
            AcquireSRWLockExclusive(&shard.lock);
            for (auto& pair : shard.map) {
                if (pair.second) {
                    release(pair.second);
                    delete pair.second;
                }
            }
            shard.map.clear();
            ReleaseSRWLockExclusive(&shard.lock);
        }
    }
};

} // namespace

// Mapping tables
static ShardedTable<PthreadMutexInternal>   s_mutex_table;
static ShardedTable<PthreadCondInternal>    s_cond_table;
static ShardedTable<PthreadRwlockInternal>  s_rwlock_table;
static ShardedTable<PthreadBarrierInternal> s_barrier_table;
static ShardedTable<PthreadSpinInternal>    s_spin_table;

/* Thread tables are only synchronized during create and join. */
static std::unordered_map<uint32_t, PthreadThreadInternal*> s_thread_map;
static std::unordered_map<DWORD, uint32_t>                  s_wintid_to_linuxtid;

// Generates unique Linux thread IDs
static volatile uint32_t s_next_thread_id = 1000;

// ============================================================
// Initialize / Shutdown
// ============================================================

void PthreadMapper::Initialize() {
    PERF_PROFILE_SCOPE("PthreadMap");

    if (s_initialized) return;
    
    InitializeSRWLock(&s_lock);
    s_initialized = true;
    
    uint32_t main_tid;
    PthreadThreadInternal* main_thread = CreateThread(&main_tid);
    if (main_thread) {
        main_thread->handle = GetCurrentThread();
        main_thread->win_thread_id = GetCurrentThreadId();
        main_thread->detached = 0;
        main_thread->exited = 0;
        
        AcquireSRWLockExclusive(&s_lock);
        s_wintid_to_linuxtid[main_thread->win_thread_id] = main_tid;
        ReleaseSRWLockExclusive(&s_lock);
    }
}

void PthreadMapper::Shutdown() {
    PERF_PROFILE_SCOPE("PthreadMap");

    if (!s_initialized) return;
    
    s_mutex_table.Clear([](PthreadMutexInternal* mutex) { DeleteCriticalSection(&mutex->cs); });
    s_cond_table.Clear([](PthreadCondInternal*) {});
    s_rwlock_table.Clear([](PthreadRwlockInternal*) {});
    s_barrier_table.Clear([](PthreadBarrierInternal* barrier) { DeleteCriticalSection(&barrier->cs); });
    s_spin_table.Clear([](PthreadSpinInternal*) {});

    AcquireSRWLockExclusive(&s_lock);

    // Cleanup threads
    for (auto& pair : s_thread_map) {
        if (pair.second) {
            if (pair.second->handle && pair.second->handle != INVALID_HANDLE_VALUE) {
                // Don't close if it's the current thread's pseudo-handle
                if (pair.second->handle != GetCurrentThread()) {
                    CloseHandle(pair.second->handle);
                }
            }
            delete pair.second;
        }
    }
    s_thread_map.clear();
    s_wintid_to_linuxtid.clear();
    
    ReleaseSRWLockExclusive(&s_lock);
    s_initialized = false;
}

// ============================================================
// Mutex Mapping
// ============================================================

PthreadMutexInternal* PthreadMapper::GetOrCreateMutex(void* linux_mutex) {
    PERF_PROFILE_SCOPE("PthreadMap");

    if (PthreadMutexInternal *cached = fastMutexLookup(linux_mutex))
        return cached;

    PthreadMutexInternal *result = s_mutex_table.GetOrCreate(linux_mutex, MUTEX_INIT_MAGIC, [] {
        PthreadMutexInternal* mutex = new PthreadMutexInternal;
        InitializeCriticalSection(&mutex->cs);
        mutex->type = LINUX_PTHREAD_MUTEX_DEFAULT;
        mutex->owner_thread = 0;
        mutex->lock_count = 0;
        mutex->initialized = MUTEX_INIT_MAGIC;
        return mutex;
    });
    rememberFastMutex(linux_mutex, result);
    return result;
}

PthreadMutexInternal* PthreadMapper::FindMutex(void* linux_mutex) {
    PERF_PROFILE_SCOPE("PthreadMap");

    if (PthreadMutexInternal *cached = fastMutexLookup(linux_mutex))
        return cached;

    PthreadMutexInternal *result = s_mutex_table.Find(linux_mutex, MUTEX_INIT_MAGIC);
    if (result)
        rememberFastMutex(linux_mutex, result);
    return result;
}

void PthreadMapper::DestroyMutex(void* linux_mutex) {
    PERF_PROFILE_SCOPE("PthreadMap");

    rememberFastMutex(linux_mutex, nullptr);

    s_mutex_table.Destroy(linux_mutex,
                          [](PthreadMutexInternal* mutex) { DeleteCriticalSection(&mutex->cs); });
}

// ============================================================
// Cond Mapping
// ============================================================

PthreadCondInternal* PthreadMapper::GetOrCreateCond(void* linux_cond) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_cond_table.GetOrCreate(linux_cond, COND_INIT_MAGIC, [] {
        PthreadCondInternal* cond = new PthreadCondInternal;
        InitializeConditionVariable(&cond->cv);
        cond->clock_id = 0;  // CLOCK_REALTIME
        cond->initialized = COND_INIT_MAGIC;
        return cond;
    });
}

PthreadCondInternal* PthreadMapper::FindCond(void* linux_cond) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_cond_table.Find(linux_cond, COND_INIT_MAGIC);
}

void PthreadMapper::DestroyCond(void* linux_cond) {
    PERF_PROFILE_SCOPE("PthreadMap");

    s_cond_table.Destroy(linux_cond, [](PthreadCondInternal*) {});
}

// ============================================================
// Rwlock Mapping
// ============================================================

PthreadRwlockInternal* PthreadMapper::GetOrCreateRwlock(void* linux_rwlock) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_rwlock_table.GetOrCreate(linux_rwlock, RWLOCK_INIT_MAGIC, [] {
        PthreadRwlockInternal* rwlock = new PthreadRwlockInternal;
        InitializeSRWLock(&rwlock->srw);
        rwlock->initialized = RWLOCK_INIT_MAGIC;
        return rwlock;
    });
}

PthreadRwlockInternal* PthreadMapper::FindRwlock(void* linux_rwlock) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_rwlock_table.Find(linux_rwlock, RWLOCK_INIT_MAGIC);
}

void PthreadMapper::DestroyRwlock(void* linux_rwlock) {
    PERF_PROFILE_SCOPE("PthreadMap");

    s_rwlock_table.Destroy(linux_rwlock, [](PthreadRwlockInternal*) {});
}

// ============================================================
// Barrier Mapping
// ============================================================

PthreadBarrierInternal* PthreadMapper::GetOrCreateBarrier(void* linux_barrier, unsigned int count) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_barrier_table.GetOrCreate(linux_barrier, BARRIER_INIT_MAGIC, [count] {
        PthreadBarrierInternal* barrier = new PthreadBarrierInternal;
        InitializeCriticalSection(&barrier->cs);
        InitializeConditionVariable(&barrier->cv);
        barrier->threshold = count;
        barrier->count = 0;
        barrier->generation = 0;
        barrier->initialized = BARRIER_INIT_MAGIC;
        return barrier;
    });
}

PthreadBarrierInternal* PthreadMapper::FindBarrier(void* linux_barrier) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_barrier_table.Find(linux_barrier, BARRIER_INIT_MAGIC);
}

void PthreadMapper::DestroyBarrier(void* linux_barrier) {
    PERF_PROFILE_SCOPE("PthreadMap");

    s_barrier_table.Destroy(linux_barrier,
                            [](PthreadBarrierInternal* barrier) { DeleteCriticalSection(&barrier->cs); });
}

// ============================================================
// Spin Mapping
// ============================================================

PthreadSpinInternal* PthreadMapper::GetOrCreateSpin(void* linux_spin) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_spin_table.GetOrCreate(linux_spin, SPIN_INIT_MAGIC, [] {
        PthreadSpinInternal* spin = new PthreadSpinInternal;
        spin->lock = 0;
        spin->initialized = SPIN_INIT_MAGIC;
        return spin;
    });
}

PthreadSpinInternal* PthreadMapper::FindSpin(void* linux_spin) {
    PERF_PROFILE_SCOPE("PthreadMap");

    return s_spin_table.Find(linux_spin, SPIN_INIT_MAGIC);
}

void PthreadMapper::DestroySpin(void* linux_spin) {
    PERF_PROFILE_SCOPE("PthreadMap");

    s_spin_table.Destroy(linux_spin, [](PthreadSpinInternal*) {});
}

// ============================================================
// Thread Mapping
// ============================================================

PthreadThreadInternal* PthreadMapper::CreateThread(uint32_t* out_linux_tid) {
    PERF_PROFILE_SCOPE("PthreadMap");

    AcquireSRWLockExclusive(&s_lock);
    
    uint32_t tid = InterlockedIncrement((volatile LONG*)&s_next_thread_id);
    
    PthreadThreadInternal* thread = new PthreadThreadInternal;
    memset(thread, 0, sizeof(PthreadThreadInternal));
    thread->linux_thread_id = tid;
    
    s_thread_map[tid] = thread;
    
    if (out_linux_tid) {
        *out_linux_tid = tid;
    }
    
    ReleaseSRWLockExclusive(&s_lock);
    return thread;
}

void PthreadMapper::RegisterThread(uint32_t linux_tid, DWORD win_tid) {
    PERF_PROFILE_SCOPE("PthreadMap");

    AcquireSRWLockExclusive(&s_lock);

    auto it = s_thread_map.find(linux_tid);
    if (it != s_thread_map.end() && it->second) {
        it->second->win_thread_id = win_tid;
        s_wintid_to_linuxtid[win_tid] = linux_tid;
    }

    ReleaseSRWLockExclusive(&s_lock);
}

PthreadThreadInternal* PthreadMapper::FindThread(uint32_t linux_tid) {
    PERF_PROFILE_SCOPE("PthreadMap");

    AcquireSRWLockShared(&s_lock);
    
    auto it = s_thread_map.find(linux_tid);
    PthreadThreadInternal* result = (it != s_thread_map.end()) ? it->second : nullptr;
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

PthreadThreadInternal* PthreadMapper::FindThreadByWinId(DWORD win_tid) {
    PERF_PROFILE_SCOPE("PthreadMap");

    AcquireSRWLockShared(&s_lock);
    
    PthreadThreadInternal* result = nullptr;
    auto it = s_wintid_to_linuxtid.find(win_tid);
    if (it != s_wintid_to_linuxtid.end()) {
        auto thread_it = s_thread_map.find(it->second);
        if (thread_it != s_thread_map.end()) {
            result = thread_it->second;
        }
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyThread(uint32_t linux_tid) {
    PERF_PROFILE_SCOPE("PthreadMap");

    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_thread_map.find(linux_tid);
    if (it != s_thread_map.end()) {
        PthreadThreadInternal* thread = it->second;
        if (thread) {
            // Remove from wintid map
            s_wintid_to_linuxtid.erase(thread->win_thread_id);
            
            if (thread->handle && thread->handle != INVALID_HANDLE_VALUE &&
                thread->handle != GetCurrentThread()) {
                CloseHandle(thread->handle);
            }
            delete thread;
        }
        s_thread_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

uint32_t PthreadMapper::GetCurrentLinuxTid() {
    PERF_PROFILE_SCOPE("PthreadMap");

    DWORD win_tid = GetCurrentThreadId();
    
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_wintid_to_linuxtid.find(win_tid);
    uint32_t result = (it != s_wintid_to_linuxtid.end()) ? it->second : 0;
    
    ReleaseSRWLockShared(&s_lock);
    
    // If not found, this thread wasn't created via pthread_create
    // Create an entry for it
    if (result == 0) {
        uint32_t new_tid;
        PthreadThreadInternal* thread = CreateThread(&new_tid);
        if (thread) {
            thread->handle = GetCurrentThread();
            thread->win_thread_id = win_tid;
            thread->detached = 1;  // Treat as detached
            
            AcquireSRWLockExclusive(&s_lock);
            s_wintid_to_linuxtid[win_tid] = new_tid;
            ReleaseSRWLockExclusive(&s_lock);
            
            result = new_tid;
        }
    }
    
    return result;
}
