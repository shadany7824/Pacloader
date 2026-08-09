#include "es1Wmmt4Card.hpp"
#include "../es1CompatLayer.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace
{
/* The reader library is linked statically, so there are no BngRw symbols; these
 * entry points were found by their shared prologue.  Each takes the device id
 * first, and i386 cdecl lets a hook declare only the arguments it needs. */
constexpr uintptr_t BanaReaderReadyAddress = 0x8ac3e40;
constexpr uintptr_t BanaReaderStartAddress = 0x8ac3182;
constexpr uintptr_t BanaResetAddress = 0x8ac39b0;    /* (dev, callback, user) */
constexpr uintptr_t BanaReqLedAddress = 0x8ac3774;   /* (dev, value, callback, user) */
constexpr uintptr_t BanaReqBeepAddress = 0x8ac3654;  /* (dev, value, callback, user) */
constexpr uintptr_t BanaReqAuthAddress = 0x8ac3ad6;  /* (dev, a, b, c, d) */
constexpr uintptr_t BanaWaitTouchAddress = 0x8ac387e;/* (dev, a, b, touchCallback, user) */
constexpr uintptr_t BanaAttachAddress = 0x8ac3ecc;   /* (dev, a, b, c, callback, user) */

/* Number of readers the library itself accepts, from its own bounds check. */
constexpr int BanaMaximumDevices = 2;
constexpr int BanaResultOk = 1;
constexpr int BanaResultBadDevice = -100;
constexpr int BanaStatusOk = 0;

std::atomic<unsigned int> g_hookTraceMask{0};

void traceHook(unsigned int bit, const char *name)
{
    const unsigned int mask = 1u << bit;
    if ((g_hookTraceMask.fetch_or(mask) & mask) == 0)
    {
        log_info("System ES1 WMMT4: %s hook invoked", name);
        std::fflush(stdout);
    }
}

bool badDevice(int deviceId)
{
    return deviceId < 0 || deviceId >= BanaMaximumDevices;
}

/* Completion callback the reader library invokes when an asynchronous request
 * finishes.  IcCard passes its own handler plus itself as user data. */
using BanaCommandCallback = void (*)(int deviceId, int status, void *userData);
using BanaTouchCallback = void (*)(int deviceId, int status, void *cardRecord, void *userData);

/* Run IcCard's completion handler as if the reader had answered immediately. */
void banaComplete(BanaCommandCallback callback, int deviceId, void *userData)
{
    if (!callback)
        return;
    GuestTls::EnterGuestCode();
    callback(deviceId, BanaStatusOk, userData);
    GuestTls::EnterHostCall();
}

extern "C" int wmmt4BanaReaderReady(int deviceId)
{
    GuestTls::HostCallScope hostCall;
    traceHook(0, "IC card reader ready check");
    return badDevice(deviceId) ? BanaResultBadDevice : BanaResultOk;
}

extern "C" int wmmt4BanaReaderStart(int deviceId)
{
    GuestTls::HostCallScope hostCall;
    traceHook(1, "IC card reader start");
    return badDevice(deviceId) ? BanaResultBadDevice : BanaResultOk;
}

extern "C" int wmmt4BanaReset(int deviceId, BanaCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(2, "IC card reader reset");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    banaComplete(callback, deviceId, userData);
    return BanaResultOk;
}

extern "C" int wmmt4BanaAttach(int deviceId, int, int, int, BanaCommandCallback callback,
                               void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(3, "IC card reader attach");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    banaComplete(callback, deviceId, userData);
    return BanaResultOk;
}

extern "C" int wmmt4BanaReqLed(int deviceId, int, BanaCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(4, "IC card reader LED request");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    banaComplete(callback, deviceId, userData);
    return BanaResultOk;
}

extern "C" int wmmt4BanaReqBeep(int deviceId, int, BanaCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(5, "IC card reader beep request");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    banaComplete(callback, deviceId, userData);
    return BanaResultOk;
}

extern "C" int wmmt4BanaReqAuth(int deviceId, int, int, int, void *)
{
    GuestTls::HostCallScope hostCall;
    traceHook(6, "IC card reader auth request");
    return badDevice(deviceId) ? BanaResultBadDevice : BanaResultOk;
}

extern "C" int wmmt4BanaWaitTouch(int deviceId, int, int, BanaTouchCallback, void *)
{
    GuestTls::HostCallScope hostCall;
    traceHook(7, "IC card reader wait-touch request");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    /* Accept the request but leave it pending: no card is on the reader.  The
     * boot check only needs the request to be accepted; completing it means
     * handing over a 168-byte card record, which is the next piece of work. */
    return BanaResultOk;
}
} // namespace

extern "C" int wmmt4InstallCardHooks(void)
{
    const Es1HookSpec hooks[] = {
        {BanaReaderReadyAddress, reinterpret_cast<void *>(wmmt4BanaReaderReady),
         "BngRw_reader_ready"},
        {BanaReaderStartAddress, reinterpret_cast<void *>(wmmt4BanaReaderStart),
         "BngRw_reader_start"},
        {BanaAttachAddress, reinterpret_cast<void *>(wmmt4BanaAttach), "BngRwAttach"},
        {BanaResetAddress, reinterpret_cast<void *>(wmmt4BanaReset), "BngRwDevReset"},
        {BanaReqLedAddress, reinterpret_cast<void *>(wmmt4BanaReqLed), "BngRwReqLed"},
        {BanaReqBeepAddress, reinterpret_cast<void *>(wmmt4BanaReqBeep), "BngRwReqBeep"},
        {BanaReqAuthAddress, reinterpret_cast<void *>(wmmt4BanaReqAuth), "BngRwReqAiccAuth"},
        {BanaWaitTouchAddress, reinterpret_cast<void *>(wmmt4BanaWaitTouch),
         "BngRwReqWaitTouch"},
    };
    return es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
}
