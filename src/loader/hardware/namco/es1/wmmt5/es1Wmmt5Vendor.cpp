#include "es1Wmmt5Vendor.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* The STR400 blank-card vendor, reported as disconnected (E2301) when nothing
 * answers. A frame is STX, command, length, payload, checksum, ETX. */
namespace
{

constexpr uint8_t FrameStart = 0x02;
constexpr uint8_t FrameEnd = 0x03;

constexpr uint8_t CommandReset = 0x30;
constexpr uint8_t CommandCarry = 0x31;
constexpr uint8_t CommandStatus = 0x32;
constexpr uint8_t CommandEject = 0x34;
constexpr uint8_t CommandCarryDrive = 0x41;

/* Status flags the title reads out of the reply's second payload byte. */
constexpr uint8_t StatusNotReady = 0x20;
constexpr uint8_t StatusReady = 0x40;
constexpr uint8_t StatusHasCards = 0x08;

std::mutex g_mutex;
std::deque<std::vector<uint8_t>> g_replies;
bool g_reset = false;

void queueReply(uint8_t command, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 6);
    frame.push_back(FrameStart);
    frame.push_back(command);
    frame.push_back(0);
    frame.push_back(static_cast<uint8_t>(payload.size()));
    for (uint8_t byte : payload)
        frame.push_back(byte);

    uint8_t checksum = static_cast<uint8_t>(command + payload.size());
    for (uint8_t byte : payload)
        checksum = static_cast<uint8_t>(checksum + byte);
    frame.push_back(checksum);
    frame.push_back(FrameEnd);

    g_replies.push_back(std::move(frame));
}

void answer(const uint8_t *data, int length)
{
    if (length < 3 || data[0] != FrameStart || data[length - 1] != FrameEnd)
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    switch (data[1])
    {
    case CommandReset:
        g_reset = true;
        queueReply(CommandReset, {'1'});
        break;
    case CommandCarry:
        queueReply(CommandCarry, {'0'});
        break;
    case CommandStatus:
    {
        uint8_t flags = g_reset ? StatusReady : StatusNotReady;
        /* Only a terminal cabinet holds a stack of blank cards. */
        if (g_reset && getConfig()->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL)
            flags = static_cast<uint8_t>(flags | StatusHasCards);
        queueReply(CommandStatus, {'1', flags, '1', 0});
        break;
    }
    case CommandEject:
        queueReply(CommandEject, {'1'});
        break;
    case CommandCarryDrive:
        queueReply(CommandCarryDrive, {'1'});
        break;
    default:
        break;
    }
}

int wmmt5VendorSend(int, const void *data, int length)
{
    GuestTls::HostCallScope hostCall;
    if (data && length > 0)
        answer(static_cast<const uint8_t *>(data), length);
    return length;
}

int wmmt5VendorReceive(int, void *data, int length)
{
    GuestTls::HostCallScope hostCall;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!data || g_replies.empty())
        return 0;

    const std::vector<uint8_t> &reply = g_replies.front();
    if (length > 0 && static_cast<size_t>(length) < reply.size())
        return 0;
    std::memcpy(data, reply.data(), reply.size());
    const int size = static_cast<int>(reply.size());
    g_replies.pop_front();
    return size;
}

constexpr uintptr_t SendAddress = 0x080eefc0;
constexpr uintptr_t ReceiveAddress = 0x080eef50;
constexpr uint8_t TransferSignature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x8b, 0x45, 0x08};

} // namespace

void es1Wmmt5InstallVendorHooks(void)
{
    const Es1HookSpec hooks[] = {
        {SendAddress, reinterpret_cast<void *>(wmmt5VendorSend), "str400Send", nullptr,
         TransferSignature, sizeof(TransferSignature)},
        {ReceiveAddress, reinterpret_cast<void *>(wmmt5VendorReceive), "str400Receive", nullptr,
         TransferSignature, sizeof(TransferSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 card vendor");
}

#endif
