#include "es1Jvs.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <cstdio>
#include <vector>

#include "../../../config/config.h"
#include "../../common/jvs.h"
#include "../../../log/log.h"

namespace
{
constexpr int Es1JvsDescriptor = 0x4e12;
constexpr char Es1JvsDevicePath[] = "/dev/ttyS2";
constexpr size_t MaximumQueuedBytes = 8 * 1024;

std::mutex bufferMutex;
/* processPacket() uses jvs.c's global packet buffers, so serialize that
 * separately: a guest write must not corrupt a frame while the queue is open. */
std::mutex processMutex;
std::deque<uint8_t> requestBytes;
std::deque<uint8_t> replyBytes;
bool opened = false;
unsigned long requestFrames = 0;
unsigned long responseFrames = 0;
unsigned long malformedFrames = 0;
unsigned long readCalls = 0;
unsigned long emptyReads = 0;
unsigned long writeCalls = 0;
unsigned long ioctlCalls = 0;

bool diagnosticsEnabled()
{
    return getConfig()->namcoES1.serialDiagnostics != 0;
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

/*
 * Return the number of escaped bytes occupied by the first complete frame.
 * A zero return means that more bytes are needed.  A stray byte or an
 * incomplete frame overtaken by a new SYNC returns its byte count so the
 * caller can discard it without feeding malformed data to readPacket().
 */
size_t framedLength(const std::deque<uint8_t> &bytes, bool &complete)
{
    complete = false;
    if (bytes.empty())
        return 0;
    if (bytes.front() != SYNC)
        return 1;

    size_t raw = 1;
    size_t decoded = 0;
    size_t needed = 2; // destination and length

    while (decoded < needed)
    {
        if (raw >= bytes.size())
            return 0;

        uint8_t value = bytes[raw];
        if (value == SYNC)
            return raw;

        if (value == ESCAPE)
        {
            if (raw + 1 >= bytes.size())
                return 0;
            value = static_cast<uint8_t>(bytes[raw + 1] + 1);
            raw += 2;
        }
        else
        {
            ++raw;
        }

        ++decoded;
        if (decoded == 2)
        {
            needed = 2 + static_cast<size_t>(value);
            if (needed > JVS_MAX_PACKET_SIZE + 2)
                return 1;
        }
    }

    complete = true;
    return raw;
}

void answerRequest(const uint8_t *frame, size_t length)
{
    if (length == 0 || length > sizeof(inputBuffer))
        return;

    std::lock_guard<std::mutex> processLock(processMutex);

    /* readPacket() expects the complete escaped frame in inputBuffer. */
    std::memcpy(inputBuffer, frame, length);
    std::memset(inputBuffer + length, 0, sizeof(inputBuffer) - length);

    int packetSize = 0;
    const JVSStatus status = processPacket(&packetSize);
    if (status != JVS_STATUS_SUCCESS || packetSize <= 0)
    {
        if (diagnosticsEnabled())
        {
            char bytes[96];
            formatBytes(frame, length, bytes, sizeof(bytes));
            log_warn("System ES1 JVS diag: request rejected status=%d length=%zu data=%s",
                     static_cast<int>(status), length, bytes);
        }
        return;
    }

    /* A JVS reset changes the sense line; it is not answered by the slave. */
    if (inputPacket.length > 0 && inputPacket.data[0] == CMD_RESET)
        return;

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (replyBytes.size() + static_cast<size_t>(packetSize) > MaximumQueuedBytes)
        replyBytes.clear();
    replyBytes.insert(replyBytes.end(), outputBuffer, outputBuffer + packetSize);
    ++requestFrames;
    ++responseFrames;
    if (diagnosticsEnabled() && (requestFrames == 1 || requestFrames % 120 == 0))
    {
        char requestText[96];
        char responseText[96];
        formatBytes(frame, length, requestText, sizeof(requestText));
        formatBytes(outputBuffer, static_cast<size_t>(packetSize), responseText,
                    sizeof(responseText));
        log_info("System ES1 JVS diag: frame #%lu request=%s response=%s queue=%zu",
                 requestFrames, requestText, responseText, replyBytes.size());
    }
}

void consumeRequests()
{
    std::vector<uint8_t> frame;
    frame.reserve(JVS_MAX_PACKET_SIZE + 2);
    for (;;)
    {
        frame.clear();
        bool complete = false;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            const size_t length = framedLength(requestBytes, complete);
            if (length == 0)
                return;

            if (!complete)
            {
                const uint8_t discarded = requestBytes.front();
                requestBytes.erase(requestBytes.begin());
                ++malformedFrames;
                if (diagnosticsEnabled())
                    log_warn("System ES1 JVS diag: discarded malformed/stray byte #%lu value=%02X",
                             malformedFrames, discarded);
                continue;
            }

            frame.assign(requestBytes.begin(), requestBytes.begin() + length);
            requestBytes.erase(requestBytes.begin(), requestBytes.begin() + length);
        }

        if (complete && !frame.empty())
            answerRequest(frame.data(), frame.size());
    }
}
} // namespace

extern "C" int es1JvsSerialEnabled(void)
{
    return getConfig()->platform == ARCADE_PLATFORM_NAMCO_ES1;
}

extern "C" int es1JvsSerialOpen(const char *path, int)
{
    if (!es1JvsSerialEnabled() || !path || std::strcmp(path, Es1JvsDevicePath) != 0)
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    requestBytes.clear();
    replyBytes.clear();
    requestFrames = 0;
    responseFrames = 0;
    malformedFrames = 0;
    readCalls = 0;
    emptyReads = 0;
    writeCalls = 0;
    ioctlCalls = 0;
    opened = true;
    log_info("System ES1 JVS: %s answered by the ES1-JAMMA virtual board (%s)",
             Es1JvsDevicePath, getJVSIO()->capabilities.name);
    if (diagnosticsEnabled())
        log_info("System ES1 JVS diag: open identity=%s players=%d switches=%d analogue=%d/%d",
                 getJVSIO()->capabilities.name, getJVSIO()->capabilities.players,
                 getJVSIO()->capabilities.switches, getJVSIO()->capabilities.analogueInChannels,
                 getJVSIO()->capabilities.analogueInBits);
    return Es1JvsDescriptor;
}

extern "C" int es1JvsSerialIsDescriptor(int fd)
{
    if (fd != Es1JvsDescriptor || !es1JvsSerialEnabled())
        return 0;
    std::lock_guard<std::mutex> lock(bufferMutex);
    return opened ? 1 : 0;
}

extern "C" int es1JvsSerialBytesAvailable(int fd)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(replyBytes.size());
}

extern "C" int es1JvsSerialRead(int fd, void *buffer, size_t count)
{
    if (!es1JvsSerialIsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count == 0)
        return 0;

    std::lock_guard<std::mutex> lock(bufferMutex);
    ++readCalls;
    if (replyBytes.empty())
    {
        ++emptyReads;
        if (diagnosticsEnabled() && (emptyReads == 1 || emptyReads % 20 == 0))
            log_warn("System ES1 JVS diag: READ EMPTY #%lu requested=%zu writes=%lu frames=%lu",
                     emptyReads, count, writeCalls, requestFrames);
        errno = EAGAIN;
        return -1;
    }

    const size_t taken = std::min(count, replyBytes.size());
    auto *out = static_cast<uint8_t *>(buffer);
    std::copy_n(replyBytes.begin(), taken, out);
    replyBytes.erase(replyBytes.begin(), replyBytes.begin() + taken);
    if (diagnosticsEnabled() && (readCalls == 1 || readCalls % 120 == 0))
        log_info("System ES1 JVS diag: read #%lu requested=%zu returned=%zu queue=%zu",
                 readCalls, count, taken, replyBytes.size());
    return static_cast<int>(taken);
}

extern "C" int es1JvsSerialWrite(int fd, const void *buffer, size_t count)
{
    if (!es1JvsSerialIsDescriptor(fd) || (!buffer && count))
    {
        errno = EBADF;
        return -1;
    }

    if (count)
    {
        ++writeCalls;
        const auto *in = static_cast<const uint8_t *>(buffer);
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (requestBytes.size() + count > MaximumQueuedBytes)
            {
                if (diagnosticsEnabled())
                    log_warn("System ES1 JVS diag: request queue overflow, dropping %zu bytes",
                             requestBytes.size());
                requestBytes.clear();
            }
            requestBytes.insert(requestBytes.end(), in, in + count);
        }
        consumeRequests();
    }
    return static_cast<int>(count);
}

extern "C" int es1JvsSerialClose(int fd)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return -1;

    std::lock_guard<std::mutex> lock(bufferMutex);
    if (diagnosticsEnabled())
        log_info("System ES1 JVS diag: close writes=%lu frames=%lu responses=%lu malformed=%lu emptyReads=%lu",
                 writeCalls, requestFrames, responseFrames, malformedFrames, emptyReads);
    requestBytes.clear();
    replyBytes.clear();
    opened = false;
    return 0;
}

extern "C" int es1JvsSerialIoctl(int fd, unsigned long request, void *argument)
{
    if (!es1JvsSerialIsDescriptor(fd))
        return -1;

    ++ioctlCalls;

    constexpr unsigned long LinuxFionread = 0x541B;
    if (request == LinuxFionread && argument)
    {
        *static_cast<int *>(argument) = es1JvsSerialBytesAvailable(fd);
        if (diagnosticsEnabled() && (ioctlCalls == 1 || ioctlCalls % 120 == 0))
            log_info("System ES1 JVS diag: FIONREAD #%lu=%d", ioctlCalls,
                     *static_cast<int *>(argument));
        return 0;
    }

    /* The cabinet wires the JVS sense line to CTS, and the master keeps handing
     * out addresses while it reads that bit as clear. Leaving the caller's word
     * untouched left it reading the stack, so enumeration never finished. */
    constexpr unsigned long LinuxTiocmget = 0x5415;
    constexpr int LinuxTiocmCts = 0x020;
    if (request == LinuxTiocmget && argument)
    {
        *static_cast<int *>(argument) = getSenseLine() == 1 ? LinuxTiocmCts : 0;
        return 0;
    }

    /* The ES1 game only uses termios/RS485 setup here; a queue needs neither. */
    return 0;
}

#endif
