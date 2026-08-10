#include "es1Wmmt4Card.hpp"
#include "../es1CompatLayer.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../config/config.h"
#include "../../../../log/log.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

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
constexpr size_t BanaCardRecordSize = 168;
constexpr size_t BanaChipIdOffset = 0x2c;
constexpr size_t BanaAccessCodeOffset = 0x50;
constexpr size_t BanaCardStringSize = 32;

const std::array<uint8_t, BanaCardRecordSize> DefaultCardRecord = {
    0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x92, 0x2e, 0x58, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0x5c, 0x97, 0x44, 0xf0, 0x88, 0x04, 0x00,
    0x43, 0x26, 0x2c, 0x33, 0x00, 0x04, 0x06, 0x10, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4e, 0x42, 0x47, 0x49, 0x43, 0x36, 0x00, 0x00, 0xfa, 0xe9, 0x69, 0x00,
    0xf6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::array<uint8_t, BanaCardRecordSize> g_cardRecord = DefaultCardRecord;
std::atomic<bool> g_cardLoaded{false};

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

std::string readCardValue(const char *key, const char *fallback)
{
    const char *file = getConfig()->namcoES1.icCard.cardFile;
    char value[BanaCardStringSize] = {};
    GetPrivateProfileStringA("Card", key, fallback, value, sizeof(value), file);
    return value;
}

void loadCardRecord()
{
    if (g_cardLoaded.exchange(true, std::memory_order_acq_rel))
        return;

    g_cardRecord = DefaultCardRecord;
    const std::string chipId = readCardValue(
        "ChipId", "7F5C9744F111111143262C3300040610");
    const std::string accessCode = readCardValue(
        "AccessCode", "30764352518498791337");
    std::memset(g_cardRecord.data() + BanaChipIdOffset, 0, BanaCardStringSize);
    std::memset(g_cardRecord.data() + BanaAccessCodeOffset, 0, BanaCardStringSize);
    std::memcpy(g_cardRecord.data() + BanaChipIdOffset, chipId.c_str(),
                chipId.size() < BanaCardStringSize - 1 ? chipId.size() : BanaCardStringSize - 1);
    std::memcpy(g_cardRecord.data() + BanaAccessCodeOffset, accessCode.c_str(),
                accessCode.size() < BanaCardStringSize - 1 ? accessCode.size() : BanaCardStringSize - 1);
    if (getConfig()->namcoES1.icCard.diagnostics)
        log_info("System ES1 WMMT4 IC card: chip=%s access=%s file=%s", chipId.c_str(),
                 accessCode.c_str(), getConfig()->namcoES1.icCard.cardFile);
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

extern "C" int wmmt4BanaWaitTouch(int deviceId, int, int, BanaTouchCallback callback,
                                   void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(7, "IC card reader wait-touch request");
    if (badDevice(deviceId))
        return BanaResultBadDevice;
    if (!getConfig()->namcoES1.icCard.enabled ||
        !getConfig()->namcoES1.icCard.autoInsert || !callback)
        return BanaResultOk;
    loadCardRecord();
    GuestTls::EnterGuestCode();
    callback(deviceId, BanaStatusOk, g_cardRecord.data(), userData);
    GuestTls::EnterHostCall();
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
