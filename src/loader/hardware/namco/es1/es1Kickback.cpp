#include "es1Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "../../../platform/platformBackend.h"
#include "../../../log/log.h"
#include "es1.h"
#include "es1Title.h"

namespace
{
constexpr int Descriptor = 0x4e20;
constexpr size_t CommandLength = 10;
constexpr size_t MaximumQueuedBytes = 1024;
constexpr uint8_t FrameHeader = 0xff;
constexpr uint8_t HealthyResult[3] = {'E', '0', '0'};
constexpr uint8_t WheelCentre[3] = {'H', 0x01, 0xff};

/* WMMT4's steering PCB is framed instead: 02, command, lengthHigh, lengthLow,
 * payload, checksum, 03, the checksum a plain sum from the command byte on. The
 * answer repeats the command with the payload length the title expects. */
constexpr uint8_t FramedStart = 0x02;
constexpr uint8_t FramedEnd = 0x03;
constexpr size_t FramedOverhead = 6;

int framedReplyLength(uint8_t command)
{
    /* Indexed from '0', as the title's own table is. */
    static const int lengths[] = {1, 16, 4, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 24};
    const int index = command - '0';
    if (index < 0 || index >= (int)(sizeof(lengths) / sizeof(lengths[0])))
        return 0;
    return lengths[index];
}

std::mutex boardMutex;
std::deque<uint8_t> commandBytes;
std::deque<uint8_t> replyBytes;
bool opened = false;
unsigned long framesSeen = 0;
unsigned int unpromptedStateReports = 0;

/* WMMT4 reads board state; report motor and self-check replies proactively. */
constexpr uint8_t MotorRunning[3] = {'C', '0', '1'};
constexpr uint8_t SelfCheckComplete[3] = {'C', '0', '6'};
constexpr unsigned int RunningReports = 1;

bool boardReportsUnprompted(void)
{
    return es1TitleQuirks()->steeringBoardReportsUnprompted != 0;
}

/* Advance the steering-board state machine; caller holds boardMutex. */
void offerState(void)
{
    if (!replyBytes.empty() || !boardReportsUnprompted())
        return;

    /* Report the centre position immediately after the initial power reply. */
    const uint8_t *state = MotorRunning;
    if (unpromptedStateReports == RunningReports)
        state = WheelCentre;
    else if (unpromptedStateReports == RunningReports + 1)
        state = HealthyResult;
    else if (unpromptedStateReports > RunningReports + 1)
        state = SelfCheckComplete;
    replyBytes.insert(replyBytes.end(), state, state + 3);
    ++unpromptedStateReports;
}

void queueReports(const uint8_t *reports, size_t reportSize, unsigned int nextReport)
{
    std::lock_guard<std::mutex> lock(boardMutex);
    if (!opened)
        return;

    replyBytes.clear();
    replyBytes.insert(replyBytes.end(), reports, reports + reportSize);
    unpromptedStateReports = nextReport;
}

void describeFrame(const uint8_t *frame, char *text, size_t size)
{
    size_t written = 0;
    for (size_t i = 0; i < CommandLength && written + 4 < size; ++i)
    {
        const int amount = std::snprintf(text + written, size - written,
                                         "%02X%s", frame[i],
                                         i + 1 == CommandLength ? "" : " ");
        if (amount <= 0)
            break;
        written += static_cast<size_t>(amount);
    }
}

/* Answers one framed command, or returns false when more bytes are needed. */
bool consumeFramedCommand(void)
{
    if (commandBytes.size() < FramedOverhead)
        return false;

    const size_t payload = ((size_t)commandBytes[2] << 8) | commandBytes[3];
    const size_t total = payload + FramedOverhead;
    if (payload > MaximumQueuedBytes || commandBytes.size() < total)
        return false;

    const uint8_t command = commandBytes[1];
    commandBytes.erase(commandBytes.begin(), commandBytes.begin() + total);

    const int replyPayload = framedReplyLength(command);
    if (replyPayload <= 0)
        return true;

    uint8_t reply[FramedOverhead + 32] = {};
    reply[0] = FramedStart;
    reply[1] = command;
    reply[2] = (uint8_t)(replyPayload >> 8);
    reply[3] = (uint8_t)replyPayload;
    uint8_t checksum = 0;
    for (int i = 1; i < 4 + replyPayload; ++i)
        checksum = (uint8_t)(checksum + reply[i]);
    reply[4 + replyPayload] = checksum;
    reply[5 + replyPayload] = FramedEnd;
    replyBytes.insert(replyBytes.end(), reply, reply + replyPayload + FramedOverhead);

    if (++framesSeen == 1)
        log_warn("System ES1 steering: first framed STR command '%c', answered %d bytes",
                 command, replyPayload + (int)FramedOverhead);
    return true;
}

void consumeCommands(void)
{
    std::lock_guard<std::mutex> lock(boardMutex);
    for (;;)
    {
        while (!commandBytes.empty() && commandBytes.front() != FrameHeader &&
               commandBytes.front() != FramedStart)
            commandBytes.pop_front();

        if (!commandBytes.empty() && commandBytes.front() == FramedStart)
        {
            if (!consumeFramedCommand())
                return;
            continue;
        }

        if (commandBytes.size() < CommandLength)
            return;

        uint8_t frame[CommandLength];
        std::copy_n(commandBytes.begin(), CommandLength, frame);
        commandBytes.erase(commandBytes.begin(), commandBytes.begin() + CommandLength);

        /* clKickback::receive() reads exactly three bytes from the STR PCB. */
        replyBytes.assign(HealthyResult, HealthyResult + sizeof(HealthyResult));

        if (++framesSeen == 1)
        {
            char text[CommandLength * 3] = {};
            describeFrame(frame, text, sizeof(text));
            log_warn("System ES1 steering: first STR frame [%s], replied E00", text);
        }
    }
}
}

extern "C" bool es1KickbackClaimsPath(const char *path)
{
    /* The serial port layout is per-title, not per-platform - another title may
     * put its IC card reader on this board's port - so the port comes from the
     * running title rather than from this file. */
    const char *devicePath = es1TitleQuirks()->kickbackDevicePath;
    return platformIsES1() && devicePath && path && std::strcmp(path, devicePath) == 0;
}

extern "C" int es1KickbackOpen(const char *path, int)
{
    if (!es1KickbackClaimsPath(path))
    {
        errno = ENOENT;
        return -1;
    }

    std::lock_guard<std::mutex> lock(boardMutex);
    commandBytes.clear();
    replyBytes.clear();
    framesSeen = 0;
    unpromptedStateReports = 0;
    if (!opened)
    {
        opened = true;
        log_warn("System ES1 steering: %s answered by the virtual STR PCB", path);
    }
    return Descriptor;
}

extern "C" int es1KickbackOwnsDescriptor(int fd)
{
    return fd == Descriptor && platformIsES1();
}

extern "C" int es1KickbackBytesAvailable(int fd)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return 0;
    std::lock_guard<std::mutex> lock(boardMutex);
    offerState();
    return static_cast<int>(replyBytes.size());
}

extern "C" int es1KickbackRead(int fd, void *buffer, size_t count)
{
    if (!es1KickbackOwnsDescriptor(fd) || !buffer)
    {
        errno = EBADF;
        return -1;
    }

    std::lock_guard<std::mutex> lock(boardMutex);
    offerState();
    if (replyBytes.empty())
    {
        errno = EAGAIN;
        return -1;
    }

    const size_t amount = std::min(count, replyBytes.size());
    std::copy_n(replyBytes.begin(), amount, static_cast<uint8_t *>(buffer));
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + amount);
    return static_cast<int>(amount);
}

extern "C" int es1KickbackWrite(int fd, const void *buffer, size_t count)
{
    if (!es1KickbackOwnsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        std::lock_guard<std::mutex> lock(boardMutex);
        if (commandBytes.size() + count > MaximumQueuedBytes)
            commandBytes.clear();
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
        commandBytes.insert(commandBytes.end(), bytes, bytes + count);
    }
    consumeCommands();
    return static_cast<int>(count);
}

extern "C" int es1KickbackClose(int fd)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(boardMutex);
    commandBytes.clear();
    replyBytes.clear();
    unpromptedStateReports = 0;
    return 0;
}

extern "C" int es1KickbackIoctl(int fd, unsigned long request, void *argument)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return -1;

    constexpr unsigned long LinuxFionread = 0x541B;
    if (request == LinuxFionread && argument)
    {
        *static_cast<int *>(argument) = es1KickbackBytesAvailable(fd);
        return 0;
    }
    return 0;
}

extern "C" void es1KickbackReportSelfCheck(void)
{
    /* Preserve the H01, E00, C06 order across separate three-byte reads. */
    constexpr uint8_t selfCheckReports[] = {
        'H', 0x01, 0xff, 'E', '0', '0', 'C', '0', '6'};
    queueReports(selfCheckReports, sizeof(selfCheckReports), RunningReports);
}

extern "C" void es1KickbackReportMotorPower(int running)
{
    const uint8_t *state = running ? MotorRunning : SelfCheckComplete;
    queueReports(state, 3, running ? 0 : RunningReports);
}

#endif
