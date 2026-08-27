#include "es1Kickback.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <chrono>

#include "../../common/jvs.h"
#include "../../../platform/platformBackend.h"
#include "../../../log/log.h"
#include "es1.h"
#include "es1StrPcbStateMachine.hpp"
#include "es1Title.h"

namespace
{
constexpr int Descriptor = 0x4e20;
constexpr size_t CommandLength = 10;
constexpr size_t MaximumQueuedBytes = 1024;
constexpr uint8_t FrameHeader = 0xff;
constexpr uint8_t HealthyResult[3] = {'E', '0', '0'};
constexpr uint8_t MotorRunning[3] = {'C', '0', '1'};
constexpr uint8_t WheelCentre[3] = {'H', 0x01, 0xff};
/* The wheel is 'H' plus a big-endian ten-bit angle, so centred is 0x01ff. */
constexpr int WheelReportMaximum = 1023;

/* WMMT4 uses framed STR packets; replies echo the command and payload size. */
constexpr uint8_t FramedStart = 0x02;
constexpr uint8_t FramedEnd = 0x03;
constexpr size_t FramedOverhead = 6;
/* The title stops polling the STR PCB in attract, and a report left across
 * that boundary is not a valid reply to the first command after resume. */
constexpr uint64_t IdleResynchronizationMilliseconds = 1000;

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
unsigned long offerCalls = 0;
unsigned long availableCalls = 0;
unsigned long readCalls = 0;
unsigned long emptyReads = 0;
unsigned long writeCalls = 0;
es1::StrPcbStateMachine strPcb;
uint64_t lastActivityMilliseconds = 0;

uint64_t monotonicMilliseconds(void)
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

constexpr uint8_t SelfCheckComplete[3] = {'C', '0', '6'};

enum class BoardTransition
{
    None,
    PowerOn,
    PowerOff,
    SelfCheck,
    PoweredSelfCheck,
};

bool boardReportsUnprompted(void)
{
    return es1TitleQuirks()->steeringBoardReportsUnprompted != 0;
}

bool boardVolunteersSelfCheck(void)
{
    return es1TitleQuirks()->steeringBoardVolunteersSelfCheck != 0;
}

bool serialDiagnosticsEnabled(void)
{
    return getConfig()->namcoES1.serialDiagnostics != 0;
}

const char *transitionName(BoardTransition transition)
{
    switch (transition)
    {
        case BoardTransition::PowerOn:
            return "power-on";
        case BoardTransition::PowerOff:
            return "power-off";
        case BoardTransition::SelfCheck:
            return "self-check";
        case BoardTransition::PoweredSelfCheck:
            return "powered-self-check";
        case BoardTransition::None:
            return "none";
    }
    return "unknown";
}

void formatBytes(const uint8_t *bytes, size_t count, char *output, size_t outputSize)
{
    size_t written = 0;
    if (!output || outputSize == 0)
        return;
    output[0] = '\0';
    for (size_t i = 0; i < count && written + 4 < outputSize; ++i)
    {
        const int amount = std::snprintf(output + written, outputSize - written,
                                         "%02X%s", bytes[i], i + 1 == count ? "" : " ");
        if (amount <= 0)
            break;
        written += static_cast<size_t>(amount);
    }
}

/* The wheel as the board would report it, from the axis the input layer binds. */
void wheelReport(uint8_t report[3])
{
    report[0] = WheelCentre[0];
    int angle = (WheelCentre[1] << 8) | WheelCentre[2];
    const JVSIO *jvs = getJVSIO();
    if (jvs && jvs->analogueMax > 0)
    {
        const long long raw = jvs->state.analogueChannel[ANALOGUE_1];
        angle = static_cast<int>(raw * WheelReportMaximum / jvs->analogueMax);
        if (angle < 0)
            angle = 0;
        else if (angle > WheelReportMaximum)
            angle = WheelReportMaximum;
    }
    report[1] = static_cast<uint8_t>(angle >> 8);
    report[2] = static_cast<uint8_t>(angle);
}

/* Reset stale serial state after the title has stopped polling.  Caller holds
 * boardMutex. */
void resynchronizeAfterIdle(uint64_t nowMilliseconds)
{
    if (lastActivityMilliseconds != 0 &&
        nowMilliseconds - lastActivityMilliseconds >= IdleResynchronizationMilliseconds)
    {
        const bool staleWheelReport = replyBytes.size() == 3 && replyBytes.front() == 'H';
        const size_t droppedReplies = staleWheelReport ? replyBytes.size() : 0;
        const size_t droppedCommands = commandBytes.size();
        if (staleWheelReport)
            replyBytes.clear();
        /* A partially written command cannot be completed safely after the
         * title has gone idle, so discard it and wait for a fresh frame. */
        commandBytes.clear();
        strPcb.resynchronize(nowMilliseconds);
        if (serialDiagnosticsEnabled())
            log_warn("System ES1 steering: idle resync gap=%llums droppedReplies=%zu "
                     "droppedCommands=%zu state=%s",
                     static_cast<unsigned long long>(nowMilliseconds - lastActivityMilliseconds),
                     droppedReplies, droppedCommands, strPcb.stateName());
    }
    lastActivityMilliseconds = nowMilliseconds;
}

/* Advance the steering-board state machine; caller holds boardMutex. */
void offerState(void)
{
    const uint64_t nowMilliseconds = monotonicMilliseconds();
    resynchronizeAfterIdle(nowMilliseconds);

    if (!replyBytes.empty() || !boardReportsUnprompted())
        return;

    ++offerCalls;
    uint8_t wheel[3];
    wheelReport(wheel);
    const uint16_t position = (static_cast<uint16_t>(wheel[1]) << 8) | wheel[2];
    uint8_t report[3];
    if (strPcb.nextUnprompted(report, position, nowMilliseconds))
    {
        replyBytes.insert(replyBytes.end(), report, report + 3);
        if (serialDiagnosticsEnabled() && (offerCalls == 1 || offerCalls % 60 == 0))
            log_info("System ES1 steering diag: offer #%lu state=%s report=%02X %02X %02X position=%u queue=%zu",
                     offerCalls, strPcb.stateName(), report[0], report[1], report[2],
                     static_cast<unsigned>(position), replyBytes.size());
    }
    else if (serialDiagnosticsEnabled() && (offerCalls == 1 || offerCalls % 60 == 0))
    {
        log_warn("System ES1 steering diag: offer #%lu SILENT state=%s position=%u queue=%zu",
                 offerCalls, strPcb.stateName(), static_cast<unsigned>(position),
                 replyBytes.size());
    }
}

/* Do not expose a transient empty read before the 4 ms board period: a real
 * STR PCB is already streaming, and a long empty result becomes E20/E2212. */
void ensureStateReport(void)
{
    offerState();
    if (!replyBytes.empty() || !boardReportsUnprompted())
        return;

    const uint64_t nowMilliseconds = monotonicMilliseconds();
    strPcb.resynchronize(nowMilliseconds);
    offerState();
}

void queueReports(const uint8_t *reports, size_t reportSize, BoardTransition transition)
{
    std::lock_guard<std::mutex> lock(boardMutex);
    if (!opened)
        return;

    const char *stateBefore = strPcb.stateName();
    const size_t droppedBytes = replyBytes.size();
    replyBytes.clear();
    replyBytes.insert(replyBytes.end(), reports, reports + reportSize);
    switch (transition)
    {
        case BoardTransition::PowerOn:
            strPcb.powerOn();
            break;
        case BoardTransition::PowerOff:
            strPcb.powerOff();
            break;
        case BoardTransition::SelfCheck:
            strPcb.beginSelfCheck();
            break;
        case BoardTransition::PoweredSelfCheck:
            strPcb.reportPoweredSelfCheck();
            break;
        case BoardTransition::None:
            break;
    }
    if (serialDiagnosticsEnabled())
    {
        char bytes[64];
        formatBytes(reports, reportSize, bytes, sizeof(bytes));
        log_info("System ES1 steering diag: queue transition=%s bytes=%s dropped=%zu state=%s->%s queue=%zu",
                 transitionName(transition), bytes, droppedBytes, stateBefore,
                 strPcb.stateName(), replyBytes.size());
    }
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

    /* A wheel report is unsolicited data. Prioritise the command response so an
     * idle report cannot shift the framed reply into STR PCB E2212. */
    if (replyBytes.size() == 3 && replyBytes.front() == 'H')
        replyBytes.clear();

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
    /* Resolve the serial path from the running title, not the platform. */
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
    offerCalls = 0;
    availableCalls = 0;
    readCalls = 0;
    emptyReads = 0;
    writeCalls = 0;
    strPcb.reset(boardReportsUnprompted(), boardVolunteersSelfCheck());
    lastActivityMilliseconds = monotonicMilliseconds();
    if (!opened)
        log_warn("System ES1 steering: %s answered by the virtual STR PCB", path);
    opened = true;
    if (serialDiagnosticsEnabled())
        log_info("System ES1 steering diag: open state=%s reportsUnprompted=%d volunteersSelfCheck=%d",
                 strPcb.stateName(), boardReportsUnprompted() ? 1 : 0,
                 boardVolunteersSelfCheck() ? 1 : 0);
    return Descriptor;
}

extern "C" int es1KickbackOwnsDescriptor(int fd)
{
    if (fd != Descriptor || !platformIsES1())
        return 0;
    std::lock_guard<std::mutex> lock(boardMutex);
    return opened ? 1 : 0;
}

extern "C" int es1KickbackBytesAvailable(int fd)
{
    if (!es1KickbackOwnsDescriptor(fd))
        return 0;
    std::lock_guard<std::mutex> lock(boardMutex);
    ensureStateReport();
    ++availableCalls;
    if (serialDiagnosticsEnabled() && (availableCalls == 1 || availableCalls % 60 == 0))
        log_info("System ES1 steering diag: available #%lu=%zu state=%s offers=%lu emptyReads=%lu",
                 availableCalls, replyBytes.size(), strPcb.stateName(), offerCalls,
                 emptyReads);
    return static_cast<int>(replyBytes.size());
}

extern "C" int es1KickbackRead(int fd, void *buffer, size_t count)
{
    if (!es1KickbackOwnsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count == 0)
        return 0;

    std::lock_guard<std::mutex> lock(boardMutex);
    ensureStateReport();
    ++readCalls;
    if (replyBytes.empty())
    {
        ++emptyReads;
        if (serialDiagnosticsEnabled() && (emptyReads == 1 || emptyReads % 10 == 0))
            log_warn("System ES1 steering diag: READ EMPTY #%lu requested=%zu state=%s offers=%lu available=%lu",
                     emptyReads, count, strPcb.stateName(), offerCalls, availableCalls);
        errno = EAGAIN;
        return -1;
    }

    const size_t queuedBefore = replyBytes.size();
    const size_t amount = std::min(count, replyBytes.size());
    std::copy_n(replyBytes.begin(), amount, static_cast<uint8_t *>(buffer));
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + amount);
    if (replyBytes.empty())
        strPcb.finishSelfCheck();
    if (serialDiagnosticsEnabled() && (readCalls == 1 || readCalls % 60 == 0))
    {
        char bytes[64];
        formatBytes(static_cast<const uint8_t *>(buffer), amount, bytes, sizeof(bytes));
        log_info("System ES1 steering diag: read #%lu requested=%zu returned=%zu data=%s queue=%zu->%zu state=%s",
                 readCalls, count, amount, bytes, queuedBefore, replyBytes.size(),
                 strPcb.stateName());
    }
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
        ++writeCalls;
        std::lock_guard<std::mutex> lock(boardMutex);
        /* Resynchronise before appending a fresh command. */
        resynchronizeAfterIdle(monotonicMilliseconds());
        const size_t queuedBefore = commandBytes.size();
        if (commandBytes.size() + count > MaximumQueuedBytes)
        {
            if (serialDiagnosticsEnabled())
                log_warn("System ES1 steering diag: command queue overflow; dropping %zu bytes",
                         commandBytes.size());
            commandBytes.clear();
        }
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
        commandBytes.insert(commandBytes.end(), bytes, bytes + count);
        if (serialDiagnosticsEnabled())
        {
            char text[64];
            formatBytes(bytes, std::min(count, static_cast<size_t>(16)), text, sizeof(text));
            log_info("System ES1 steering diag: write #%lu count=%zu data=%s commandQueue=%zu->%zu",
                     writeCalls, count, text, queuedBefore, commandBytes.size());
        }
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
    strPcb.close();
    lastActivityMilliseconds = 0;
    opened = false;
    if (serialDiagnosticsEnabled())
        log_info("System ES1 steering diag: close state=%s", strPcb.stateName());
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
    queueReports(selfCheckReports, sizeof(selfCheckReports), BoardTransition::SelfCheck);
}

extern "C" void es1KickbackReportPoweredSelfCheck(void)
{
    /* Motor running, then the self-check result, then the wheel: a board the
     * title never asks separately has to volunteer the result here. */
    constexpr uint8_t poweredReports[] = {'C', '0', '1', 'E', '0', '0'};
    queueReports(poweredReports, sizeof(poweredReports), BoardTransition::PoweredSelfCheck);
}

extern "C" void es1KickbackReportMotorPower(int running)
{
    const uint8_t *state = running ? MotorRunning : SelfCheckComplete;
    /* The power reply is queued once here, so the next volunteered report is
     * the wheel position rather than the same reply a second time. */
    queueReports(state, 3, running ? BoardTransition::PowerOn : BoardTransition::PowerOff);
}

#endif
