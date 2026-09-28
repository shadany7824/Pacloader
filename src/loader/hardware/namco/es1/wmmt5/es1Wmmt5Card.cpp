#include "es1Wmmt5Card.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <windows.h>

#include "es1Wmmt5Build.hpp"
#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../banapassport/banapassport.hpp"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* The Banapassport reader WMN5r wants on /dev/ttyS3 (E0712 without it). This
 * build answers into a caller block - result at +8, card record at +16. */
namespace
{

constexpr int BanaResultOk = 1;
constexpr int BanaResultNoCard = -1;
constexpr int BanaStatusOk = 0;
/* This build answers into a caller block: result at +8, card record at +16. */
constexpr size_t ResultOffset = 8;
constexpr size_t RecordOffset = 16;

/* The loader config only says where the reader's own file lives. */
void configureReader()
{
    banapassportConfigure(getConfig()->namcoES1.icCard.cardFile);
}

/* Report a command that finished cleanly. */
int completed(uint8_t *block)
{
    if (block)
        *reinterpret_cast<int *>(block + ResultOffset) = BanaStatusOk;
    return BanaResultOk;
}

void wmmt5BanaInit(void) {}

int wmmt5BanaAttach(int, int, int, int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

/* No request is ever left running, so none is ever executing. */
int wmmt5BanaIsCommandExecuting(void) { return 0; }

int wmmt5BanaReqLed(int, int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

int wmmt5BanaReqAction(int, int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

int wmmt5BanaReqBeep(int, int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

int wmmt5BanaReqCancel(void) { return BanaResultOk; }

int wmmt5BanaReqSendUrlTo(int, int, int, int, int, int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

int wmmt5BanaReset(int, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    return completed(block);
}

int wmmt5BanaReqWaitTouch(int reader, int, uint32_t, void *, uint8_t *block)
{
    GuestTls::HostCallScope hostCall;
    /* Only the first reader exists on a drive cabinet. */
    if (reader != 0 || !block)
        return BanaResultNoCard;

    configureReader();
    if (!banapassportReaderEnabled())
        return BanaResultNoCard;

    /* This entry point reports synchronously, so nothing can be left pending. */
    if (banapassportAutoInsert())
        banapassportPresent(nullptr, nullptr);
    if (!banapassportCardOnReader())
        return BanaResultNoCard;

    completed(block);
    std::memcpy(block + RecordOffset, banapassportRecord(), BANAPASSPORT_RECORD_SIZE);
    return BanaResultOk;
}

/* Prologues guard the build, not the identity; repeats are expected. */
constexpr uint8_t Frame0x28Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x28, 0x0f, 0xb6};
constexpr uint8_t Frame0x38Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x38, 0x0f, 0xb6};
constexpr uint8_t Frame0x18Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x8b, 0x45};
constexpr uint8_t SendUrlSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x83, 0xec, 0x40};

} // namespace

int es1Wmmt5InstallCardHooks(void)
{
    const Wmmt5Build *build = es1Wmmt5Build();
    if (!build)
        return 0;

    banapassportConfigure(getConfig()->namcoES1.icCard.cardFile);
    if (!banapassportReaderEnabled())
    {
        log_info("System ES1 WMMT5: IC card emulation disabled by configuration");
        return 0;
    }

    const Es1HookSpec hooks[] = {
        {build->banaInit, reinterpret_cast<void *>(wmmt5BanaInit), "BngRwInit", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaAttach, reinterpret_cast<void *>(wmmt5BanaAttach), "BngRwAttach", nullptr,
         Frame0x38Signature, sizeof(Frame0x38Signature)},
        {build->banaIsCommandExecuting, reinterpret_cast<void *>(wmmt5BanaIsCommandExecuting),
         "BngRwIsCmdExec", nullptr, Frame0x18Signature, sizeof(Frame0x18Signature)},
        {build->banaReqLed, reinterpret_cast<void *>(wmmt5BanaReqLed), "BngRwReqLed", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaReqAction, reinterpret_cast<void *>(wmmt5BanaReqAction), "BngRwReqAction",
         nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaReqBeep, reinterpret_cast<void *>(wmmt5BanaReqBeep), "BngRwReqBeep", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaReqCancel, reinterpret_cast<void *>(wmmt5BanaReqCancel), "BngRwReqCancel",
         nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaReqSendUrlTo, reinterpret_cast<void *>(wmmt5BanaReqSendUrlTo),
         "BngRwReqSendUrlTo", nullptr, SendUrlSignature, sizeof(SendUrlSignature)},
        {build->banaReqWaitTouch, reinterpret_cast<void *>(wmmt5BanaReqWaitTouch),
         "BngRwReqWaitTouch", nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {build->banaReset, reinterpret_cast<void *>(wmmt5BanaReset), "BngRwReset", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
    };
    const int installed = es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]),
                                             "WMMT5 card");
    if (installed > 0)
        banapassportRegisterCardControl("WMMT5 Banapassport IC card");
    return installed;
}

#endif
