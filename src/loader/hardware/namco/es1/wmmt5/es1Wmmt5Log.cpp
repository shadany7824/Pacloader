#include "es1Wmmt5Log.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "../es1CompatLayer.h"
#include "../../../../log/log.h"

/* WMN5r's three loggers print nothing below severity 4; these route every
 * level to LOG_GAME, where the rest of the guest's chatter lives. */
namespace
{

constexpr uintptr_t LogAddresses[] = {0x080bc980, 0x080bca60, 0x080bcb40};

/* All three take (self, severity, format, ...). */
constexpr uint8_t SmallLogSignature[] = {0x55, 0x89, 0xe5, 0x81, 0xec, 0x28, 0x08, 0x00, 0x00};
constexpr uint8_t LargeLogSignature[] = {0x55, 0x89, 0xe5, 0x81, 0xec, 0x38, 0x20, 0x00, 0x00};

void wmmt5Log(void *self, int severity, const char *format, ...)
{
    (void)self;
    if (!format || !logIsEnabled(LOG_GAME))
        return;

    char message[2048];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length < 1)
        return;

    /* The title's own scale: 1 info, 2 warning, 3 system, 4 error. */
    static const char *const names[] = {"", "INF", "WRN", "SYS", "ERR"};
    const char *name = (severity >= 1 && severity <= 4) ? names[severity] : "LOG";

    /* Its messages carry their own newline; a second one would double-space. */
    char *tail = message + length - 1;
    while (tail >= message && (*tail == '\n' || *tail == '\r'))
        *tail-- = '\0';

    log_game("WMMT5 [%s] %s", name, message);
}

} // namespace

void es1Wmmt5InstallLogHooks(void)
{
    const Es1HookSpec hooks[] = {
        {LogAddresses[0], reinterpret_cast<void *>(wmmt5Log), "guestLog0", nullptr,
         SmallLogSignature, sizeof(SmallLogSignature)},
        {LogAddresses[1], reinterpret_cast<void *>(wmmt5Log), "guestLog1", nullptr,
         SmallLogSignature, sizeof(SmallLogSignature)},
        {LogAddresses[2], reinterpret_cast<void *>(wmmt5Log), "guestLog2", nullptr,
         LargeLogSignature, sizeof(LargeLogSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 log");
}

#endif
