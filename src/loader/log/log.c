#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../config/config.h"

#define LOG_COLOR_ENABLED 1
#define LOG_TIME_ENABLED 1
#define LOG_NO_REPEAT 0
#define LOG_REPEAT_BUFFER_SIZE 8192
#define LOG_STACK_MESSAGE_SIZE 1024

static struct timespec logStartTime = {0, 0};
static int g_logLevel = LOG_TRACE;
static unsigned int logRepeatCount;
static char logLastMessage[LOG_REPEAT_BUFFER_SIZE];

static const char *const logStyles[] = {
    "\x1b[0m", "\x1b[36m", "\x1b[0m", "\x1b[32m",
    "\x1b[33m", "\x1b[31m", "\x1b[35m",
};

static const char *const logNames[] = {"TRACE", "DEBUG", "GAME", "INFO", "WARN", "ERROR", "FATAL"};

static void logSetMessageMetadata(LogFormattedMessage *message)
{
    message->endWithNewLine =
        message->size > 0 &&
        (message->message[message->size - 1] == '\n' || message->message[message->size - 1] == '\r');

    if (LOG_NO_REPEAT && message->size + 1 < LOG_REPEAT_BUFFER_SIZE)
    {
        if (strcmp(logLastMessage, message->message) == 0)
            ++logRepeatCount;
        else
        {
            memcpy(logLastMessage, message->message, message->size + 1);
            logRepeatCount = 0;
        }
    }
    else
    {
        logRepeatCount = 0;
    }
    message->repeat = logRepeatCount;
}

static int logWriteVA(int level, const char *message, va_list args)
{
    char stackMessage[LOG_STACK_MESSAGE_SIZE];
    va_list measureArgs;
    va_copy(measureArgs, args);
    const int required = vsnprintf(stackMessage, sizeof(stackMessage), message, measureArgs);
    va_end(measureArgs);
    if (required < 0)
        return -1;

    char *allocated = NULL;
    LogFormattedMessage formatted = {stackMessage, (size_t)required, 0, 0};
    if ((size_t)required >= sizeof(stackMessage))
    {
        allocated = (char *)malloc((size_t)required + 1);
        if (!allocated)
            return -1;

        va_list formatArgs;
        va_copy(formatArgs, args);
        vsnprintf(allocated, (size_t)required + 1, message, formatArgs);
        va_end(formatArgs);
        formatted.message = allocated;
    }

    logSetMessageMetadata(&formatted);
    FILE *stream = logGetStream();
    logPrintHeader(stream, level);
    const int result = logPrintMessage(stream, formatted, level);
    free(allocated);
    return result;
}

int logVA(int level, const char *file, int line, const char *message, va_list args)
{
    (void)file;
    (void)line;

    const int result = logSanityChecks(level, message);
    return result < 1 ? result : logWriteVA(level, message, args);
}

int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    (void)file;
    (void)line;

    int result = logSanityChecks(level, message);
    if (result < 1)
        return result;
    if (level == LOG_DEBUG && getConfig()->showDebugMessages == 0)
        return 0;

    va_list args;
    va_start(args, message);
    result = logWriteVA(level, message, args);
    va_end(args);
    return result;
}

int logPrintMessage(FILE *stream, LogFormattedMessage message, int level)
{
    if (LOG_NO_REPEAT && message.repeat > 0)
    {
        if (message.repeat > 1)
        {
            printf("\r\033[K\033[1A\r\033[K");
            logPrintHeader(stream, level);
        }
        return fprintf(stream, "(last message repeated %i times)\n", message.repeat);
    }

    return message.endWithNewLine ? fputs(message.message, stream) : fprintf(stream, "%s\n", message.message);
}

int logPrintHeader(FILE *stream, int level)
{
    if (LOG_COLOR_ENABLED)
        fprintf(stream, "%s", logStyles[level]);

    if (LOG_TIME_ENABLED)
    {
        long seconds;
        long milliseconds;
        logGetElapsedTime(&seconds, &milliseconds);
        fprintf(stream, "[%04ld.%03ld] ", seconds, milliseconds);
    }

    fprintf(stream, "%s> ", logNames[level]);
    if (LOG_COLOR_ENABLED)
        fprintf(stream, "\x1b[0m");
    return 0;
}

int logSanityChecks(int level, const char *message)
{
    if (level < LOG_TRACE || level > LOG_FATAL)
    {
        log_warn("Invalid level in log.");
        return -1;
    }
    if (level < g_logLevel)
        return 0;
    if (!message)
    {
        log_warn("Called log with a NULL message.");
        return -1;
    }
    return 1;
}

FILE *logGetStream(void)
{
    return stdout;
}

LogFormattedMessage logFormatMessage(const char *message, va_list args)
{
    LogFormattedMessage result = {NULL, 0, 0, 0};
    if (!message)
        return result;

    va_list argsCopy;
    va_copy(argsCopy, args);
    const int required = vsnprintf(NULL, 0, message, argsCopy);
    va_end(argsCopy);
    if (required < 0)
        return result;

    const size_t size = (size_t)required;
    char *formatted = (char *)malloc(size + 1);
    if (!formatted)
        return result;

    vsnprintf(formatted, size + 1, message, args);
    result.message = formatted;
    result.size = size;
    logSetMessageMetadata(&result);
    return result;
}

void logInitTimer(void)
{
    clock_gettime(CLOCK_MONOTONIC, &logStartTime);
}

void logGetElapsedTime(long *seconds, long *milliseconds)
{
    struct timespec now;
    if (logStartTime.tv_sec == 0)
        logInitTimer();
    clock_gettime(CLOCK_MONOTONIC, &now);

    *seconds = now.tv_sec - logStartTime.tv_sec;
    *milliseconds = (now.tv_nsec - logStartTime.tv_nsec) / 1000000L;
    if (*milliseconds < 0)
    {
        *milliseconds += 1000;
        --(*seconds);
    }
}

void logSetMinLevel(int level)
{
    g_logLevel = level;
}

int logIsEnabled(int level)
{
    return level >= LOG_TRACE && level <= LOG_FATAL && level >= g_logLevel;
}
