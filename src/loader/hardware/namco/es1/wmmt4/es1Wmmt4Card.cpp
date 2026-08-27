#include "es1Wmmt4Card.hpp"
#include "../es1CompatLayer.h"
#include "../../banapassport/banapassport.hpp"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../config/config.h"
#include "../../../../log/log.h"

#include <atomic>
#include <cstdio>

/* WMMT4's half of the Banapassport reader; the card itself lives in
 * hardware/namco/banapassport. Statically linked, so these are found addresses. */

namespace
{
constexpr uintptr_t BanaReaderReadyAddress = 0x8ac3e40;
constexpr uintptr_t BanaReaderStartAddress = 0x8ac3182;
constexpr uintptr_t BanaResetAddress = 0x8ac39b0;    /* (dev, callback, user) */
constexpr uintptr_t BanaReqLedAddress = 0x8ac3774;   /* (dev, value, callback, user) */
constexpr uintptr_t BanaReqBeepAddress = 0x8ac3654;  /* (dev, value, callback, user) */
constexpr uintptr_t BanaReqAuthAddress = 0x8ac3ad6;  /* (dev, a, b, version, req, extra, cb, user) */
constexpr uintptr_t BanaWaitTouchAddress = 0x8ac387e;/* (dev, a, b, touchCallback, user) */
constexpr uintptr_t BanaAttachAddress = 0x8ac3ecc;   /* (dev, a, b, c, callback, user) */

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

/* The loader config only says where the reader's own file lives. */
void configureReader()
{
    banapassportConfigure(getConfig()->namcoES1.icCard.cardFile);
}

extern "C" int wmmt4BanaReaderReady(int deviceId)
{
    GuestTls::HostCallScope hostCall;
    traceHook(0, "IC card reader ready check");
    return banapassportBadDevice(deviceId) ? BANAPASSPORT_BAD_DEVICE : BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaReaderStart(int deviceId)
{
    GuestTls::HostCallScope hostCall;
    traceHook(1, "IC card reader start");
    return banapassportBadDevice(deviceId) ? BANAPASSPORT_BAD_DEVICE : BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaReset(int deviceId, BanapassportCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(2, "IC card reader reset");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    banapassportComplete(callback, deviceId, userData);
    return BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaAttach(int deviceId, int, int, int,
                               BanapassportCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(3, "IC card reader attach");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    banapassportComplete(callback, deviceId, userData);
    return BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaReqLed(int deviceId, int, BanapassportCommandCallback callback,
                               void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(4, "IC card reader LED request");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    banapassportComplete(callback, deviceId, userData);
    return BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaReqBeep(int deviceId, int, BanapassportCommandCallback callback,
                                void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(5, "IC card reader beep request");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    banapassportComplete(callback, deviceId, userData);
    return BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaReqAuth(int deviceId, int, int, const void *, const char *, const char *,
                                 BanapassportCommandCallback callback, void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(6, "IC card reader auth request");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    if (!banapassportCardOnReader())
        return BANAPASSPORT_NO_CARD;
    if (!banapassportWriteNbgicHeader(userData))
        return BANAPASSPORT_NO_CARD;

    banapassportComplete(callback, deviceId, userData);
    return BANAPASSPORT_OK;
}

extern "C" int wmmt4BanaWaitTouch(int deviceId, int, int, BanapassportTouchCallback callback,
                                   void *userData)
{
    GuestTls::HostCallScope hostCall;
    traceHook(7, "IC card reader wait-touch request");
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    configureReader();
    if (!banapassportReaderEnabled())
        return BANAPASSPORT_OK;

    return banapassportWaitTouch(deviceId, callback, userData, banapassportAutoInsert());
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
    const int installed = es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
    if (installed > 0)
    {
        /* Load here, not on first card request, so the file exists from boot. */
        configureReader();
        banapassportRegisterCardControl("WMMT4 Banapassport IC card");
    }
    return installed;
}
