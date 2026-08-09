#pragma once

/* Periodic all-thread sampler for diagnosing a hung guest without a debugger:
 * each thread's instruction pointer as module+offset, plus the guest return
 * addresses live on its stack.  Enabled by LL_WATCHDOG_SEC, in seconds. */

namespace ThreadWatchdog
{
    void Start();
}
