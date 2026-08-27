#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
    char *message;
    size_t size;
    int endWithNewLine;
    unsigned int repeat;
} LogFormattedMessage;

enum
{
    LOG_TRACE,
    LOG_DEBUG,
    LOG_GAME,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

/* The macros skip formatting work when the selected level is disabled. */
#define log_game(...)  (logIsEnabled(LOG_GAME)  ? logGeneric(LOG_GAME,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_trace(...) (logIsEnabled(LOG_TRACE) ? logGeneric(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_debug(...) (logIsEnabled(LOG_DEBUG) ? logGeneric(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_info(...)  (logIsEnabled(LOG_INFO)  ? logGeneric(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_warn(...)  (logIsEnabled(LOG_WARN)  ? logGeneric(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_error(...) (logIsEnabled(LOG_ERROR) ? logGeneric(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define log_fatal(...) (logIsEnabled(LOG_FATAL) ? logGeneric(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__) : 0)
#define logVA_game(...) logVA(LOG_GAME, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

int logGeneric(int level, const char *file, int line, const char *message, ...);
int logVA(int level, const char *file, int line, const char *message, va_list args);
int logIsEnabled(int level);
int logSanityChecks(int level, const char *message);
int logPrintHeader(FILE *stream, int level);
int logPrintMessage(FILE *stream, LogFormattedMessage formattedMessage, int level);
LogFormattedMessage logFormatMessage(const char *message, va_list args);
FILE *logGetStream(void);
void logInitTimer(void);
void logGetElapsedTime(long *seconds, long *milliseconds);
void logSetMinLevel(int level);

#ifdef __cplusplus
}
#endif
