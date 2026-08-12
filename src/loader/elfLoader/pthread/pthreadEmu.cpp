// ============================================================
// pthread_emu.cpp - Init/Shutdown and C API Exports
// ============================================================

#include "pthreadEmu.hpp"
#include "pthreadInternal.hpp"
#include "../../diagnostics/perfProfiler.hpp"

// External declaration for TLS destructor support
extern void CallTlsDestructors();

// ============================================================
// Global State
// ============================================================

static volatile bool s_emu_initialized = false;
static CRITICAL_SECTION s_init_lock;
static volatile bool s_init_lock_ready = false;

// One-time initialization for the init lock itself
static void EnsureInitLock() {

    PERF_PROFILE_SCOPE("Pthread");

    if (!s_init_lock_ready) {
        // This is a potential race, but only at very first initialization
        // In practice, Initialize() should be called from main thread first
        InitializeCriticalSection(&s_init_lock);
        s_init_lock_ready = true;
    }
}

// ============================================================
// Initialize / Shutdown
// ============================================================

void PthreadEmu::Initialize() {

    PERF_PROFILE_SCOPE("Pthread");

    EnsureInitLock();

    EnterCriticalSection(&s_init_lock);

    if (s_emu_initialized) {
        LeaveCriticalSection(&s_init_lock);
        return;
    }

    // Initialize the mapper (handles all object tracking)
    PthreadMapper::Initialize();

    s_emu_initialized = true;

    LeaveCriticalSection(&s_init_lock);
}

void PthreadEmu::Shutdown() {

    PERF_PROFILE_SCOPE("Pthread");

    EnsureInitLock();

    EnterCriticalSection(&s_init_lock);

    if (!s_emu_initialized) {
        LeaveCriticalSection(&s_init_lock);
        return;
    }

    // Call TLS destructors for main thread
    CallTlsDestructors();

    // Cleanup semaphores
    SemaphoreCleanup();

    // Shutdown the mapper (cleans up all objects)
    PthreadMapper::Shutdown();

    s_emu_initialized = false;

    LeaveCriticalSection(&s_init_lock);
}

// ============================================================
// C API Exports (optional, for direct hooking)
// ============================================================

extern "C" {

    // --- Initialization ---
    void emuPthreadInit() {

    PERF_PROFILE_SCOPE("Pthread");

        PthreadEmu::Initialize();
    }

    void emuPthreadShutdown() {

    PERF_PROFILE_SCOPE("Pthread");

        PthreadEmu::Shutdown();
    }

    // --- Thread Functions ---
    int emuPthreadCreate(void* thread, const void* attr,
        void* (*start_routine)(void*), void* arg) {

        return PthreadEmu::pthreadCreate(thread, attr, start_routine, arg);
    }

    int emuPthreadJoin(uint32_t thread, void** retval) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadJoin(thread, retval);
    }

    int emuPthreadDetach(uint32_t thread) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadDetach(thread);
    }

    void emuPthreadExit(void* retval) {

    PERF_PROFILE_SCOPE("Pthread");

        PthreadEmu::pthreadExit(retval);
    }

    uint32_t emuPthreadSelf() {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSelf();
    }

    int emuPthreadEqual(uint32_t t1, uint32_t t2) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadEqual(t1, t2);
    }

    // --- Mutex Functions ---
    int emuPthreadMutexInit(void* mutex, const void* attr) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexInit(mutex, attr);
    }

    int emuPthreadMutexDestroy(void* mutex) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexDestroy(mutex);
    }

    int emuPthreadMutexLock(void* mutex) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexLock(mutex);
    }

    int emuPthreadMutexTrylock(void* mutex) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexTrylock(mutex);
    }

    int emuPthreadMutexTimedlock(void* mutex, const struct timespec* abstime) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexTimedlock(mutex, abstime);
    }

    int emuPthreadMutexUnlock(void* mutex) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexUnlock(mutex);
    }

    // --- Mutex Attribute Functions ---
    int emuPthreadMutexattrInit(void* attr) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexattrInit(attr);
    }

    int emuPthreadMutexattrDestroy(void* attr) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexattrDestroy(attr);
    }

    int emuPthreadMutexattrSettype(void* attr, int type) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexattrSettype(attr, type);
    }

    int emuPthreadMutexattrGettype(const void* attr, int* type) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadMutexattrGettype(attr, type);
    }

    // --- Condition Variable Functions ---
    int emuPthreadCondInit(void* cond, const void* attr) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondInit(cond, attr);
    }

    int emuPthreadCondDestroy(void* cond) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondDestroy(cond);
    }

    int emuPthreadCondWait(void* cond, void* mutex) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondWait(cond, mutex);
    }

    int emuPthreadCondTimedwait(void* cond, void* mutex, const struct timespec* abstime) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondTimedwait(cond, mutex, abstime);
    }

    int emuPthreadCondSignal(void* cond) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondSignal(cond);
    }

    int emuPthreadCondBroadcast(void* cond) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadCondBroadcast(cond);
    }

    // --- Read-Write Lock Functions ---
    int emuPthreadRwlockInit(void* rwlock, const void* attr) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockInit(rwlock, attr);
    }

    int emuPthreadRwlockDestroy(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockDestroy(rwlock);
    }

    int emuPthreadRwlockRdlock(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockRdlock(rwlock);
    }

    int emuPthreadRwlockTryrdlock(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockTryrdlock(rwlock);
    }

    int emuPthreadRwlockWrlock(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockWrlock(rwlock);
    }

    int emuPthreadRwlockTrywrlock(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockTrywrlock(rwlock);
    }

    int emuPthreadRwlockUnlock(void* rwlock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadRwlockUnlock(rwlock);
    }

    // --- Barrier Functions ---
    int emuPthreadBarrierInit(void* barrier, const void* attr, unsigned int count) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadBarrierInit(barrier, attr, count);
    }

    int emuPthreadBarrierDestroy(void* barrier) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadBarrierDestroy(barrier);
    }

    int emuPthreadBarrierWait(void* barrier) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadBarrierWait(barrier);
    }

    // --- Spinlock Functions ---
    int emuPthreadSpinInit(void* lock, int pshared) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSpinInit(lock, pshared);
    }

    int emuPthreadSpinDestroy(void* lock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSpinDestroy(lock);
    }

    int emuPthreadSpinLock(void* lock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSpinLock(lock);
    }

    int emuPthreadSpinTrylock(void* lock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSpinTrylock(lock);
    }

    int emuPthreadSpinUnlock(void* lock) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSpinUnlock(lock);
    }

    // --- Once Function ---
    int emuPthreadOnce(void* once_control, void (*init_routine)(void)) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadOnce(once_control, init_routine);
    }

    // --- TLS Functions ---
    int emuPthreadKeyCreate(void* key, void (*destructor)(void*)) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadKeyCreate(key, destructor);
    }

    int emuPthreadKeyDelete(uint32_t key) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadKeyDelete(key);
    }

    int emuPthreadSetspecific(uint32_t key, const void* value) {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::pthreadSetSpecific(key, value);
    }

    void* emuPthreadGetspecific(uint32_t key) {

        return PthreadEmu::pthreadGetSpecific(key);
    }

    // --- Scheduling ---
    int emuPthreadSchedYield() {

    PERF_PROFILE_SCOPE("Pthread");

        return PthreadEmu::schedYield();
    }

} // extern "C"
