#include "es1Wmmt5Card.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <windows.h>

#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* The Banapassport reader WMN5r wants on /dev/ttyS3 (E0712 without it). This
 * build answers into a caller block - result at +8, card record at +16. */
namespace
{

constexpr int BanaResultOk = 1;
constexpr int BanaResultNoCard = -1;
constexpr int BanaStatusOk = 0;
constexpr size_t ResultOffset = 8;
constexpr size_t RecordOffset = 16;
constexpr size_t CardRecordSize = 168;
constexpr size_t ChipIdOffset = 0x2c;
constexpr size_t AccessCodeOffset = 0x50;
constexpr size_t CardStringSize = 32;

std::array<uint8_t, CardRecordSize> g_cardRecord{};
std::atomic<bool> g_cardLoaded{false};

std::string readCardValue(const char *key, const char *fallback)
{
    const char *file = getConfig()->namcoES1.icCard.cardFile;
    char value[CardStringSize] = {};
    GetPrivateProfileStringA("Card", key, fallback, value, sizeof(value), file);
    return value;
}

void copyField(size_t offset, const std::string &text)
{
    std::memset(g_cardRecord.data() + offset, 0, CardStringSize);
    const size_t length = text.size() < CardStringSize - 1 ? text.size() : CardStringSize - 1;
    std::memcpy(g_cardRecord.data() + offset, text.c_str(), length);
}

void loadCardRecord()
{
    if (g_cardLoaded.exchange(true, std::memory_order_acq_rel))
        return;

    g_cardRecord.fill(0);
    const std::string chipId = readCardValue("ChipId", "7F5C9744F111111143262C3300040610");
    const std::string accessCode = readCardValue("AccessCode", "30764352518498791337");
    copyField(ChipIdOffset, chipId);
    copyField(AccessCodeOffset, accessCode);
    if (getConfig()->namcoES1.icCard.diagnostics)
        log_info("System ES1 WMMT5 IC card: chip=%s access=%s file=%s", chipId.c_str(),
                 accessCode.c_str(), getConfig()->namcoES1.icCard.cardFile);
}

bool cardPresent()
{
    return getConfig()->namcoES1.icCard.enabled != 0 &&
           getConfig()->namcoES1.icCard.autoInsert != 0;
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
    if (reader != 0 || !cardPresent() || !block)
        return BanaResultNoCard;

    loadCardRecord();
    completed(block);
    std::memcpy(block + RecordOffset, g_cardRecord.data(), g_cardRecord.size());
    return BanaResultOk;
}

constexpr uintptr_t InitAddress = 0x0aa62c34;
constexpr uintptr_t AttachAddress = 0x0aa62764;
constexpr uintptr_t IsCommandExecutingAddress = 0x080ead50;
constexpr uintptr_t ReqLedAddress = 0x0aa6200c;
constexpr uintptr_t ReqActionAddress = 0x0aa61de6;
constexpr uintptr_t ReqBeepAddress = 0x0aa61eec;
constexpr uintptr_t ReqCancelAddress = 0x0aa61a1a;
constexpr uintptr_t ReqSendUrlToAddress = 0x0aa6236e;
constexpr uintptr_t ReqWaitTouchAddress = 0x0aa62116;
constexpr uintptr_t ResetAddress = 0x0aa62248;

/* Prologues guard the build, not the identity; repeats are expected. */
constexpr uint8_t Frame0x28Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x28, 0x0f, 0xb6};
constexpr uint8_t Frame0x38Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x38, 0x0f, 0xb6};
constexpr uint8_t Frame0x18Signature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x8b, 0x45};
constexpr uint8_t SendUrlSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x83, 0xec, 0x40};

} // namespace

int es1Wmmt5InstallCardHooks(void)
{
    if (!getConfig()->namcoES1.icCard.enabled)
    {
        log_info("System ES1 WMMT5: IC card emulation disabled by configuration");
        return 0;
    }

    const Es1HookSpec hooks[] = {
        {InitAddress, reinterpret_cast<void *>(wmmt5BanaInit), "BngRwInit", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {AttachAddress, reinterpret_cast<void *>(wmmt5BanaAttach), "BngRwAttach", nullptr,
         Frame0x38Signature, sizeof(Frame0x38Signature)},
        {IsCommandExecutingAddress, reinterpret_cast<void *>(wmmt5BanaIsCommandExecuting),
         "BngRwIsCmdExec", nullptr, Frame0x18Signature, sizeof(Frame0x18Signature)},
        {ReqLedAddress, reinterpret_cast<void *>(wmmt5BanaReqLed), "BngRwReqLed", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {ReqActionAddress, reinterpret_cast<void *>(wmmt5BanaReqAction), "BngRwReqAction",
         nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {ReqBeepAddress, reinterpret_cast<void *>(wmmt5BanaReqBeep), "BngRwReqBeep", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
        {ReqCancelAddress, reinterpret_cast<void *>(wmmt5BanaReqCancel), "BngRwReqCancel",
         nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {ReqSendUrlToAddress, reinterpret_cast<void *>(wmmt5BanaReqSendUrlTo),
         "BngRwReqSendUrlTo", nullptr, SendUrlSignature, sizeof(SendUrlSignature)},
        {ReqWaitTouchAddress, reinterpret_cast<void *>(wmmt5BanaReqWaitTouch),
         "BngRwReqWaitTouch", nullptr, Frame0x28Signature, sizeof(Frame0x28Signature)},
        {ResetAddress, reinterpret_cast<void *>(wmmt5BanaReset), "BngRwReset", nullptr,
         Frame0x28Signature, sizeof(Frame0x28Signature)},
    };
    return es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 card");
}

#endif
