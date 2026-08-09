#include "es1.h"
#include "es1Title.h"
#include "es1VirtualDevices.h"
#include "es1Network.h"
#include "es1TestModeCompat.h"

#include "../../../log/log.h"
#include "../../../graphics/sdlCalls.h"
#include "../../../elfLoader/ipcBridge.hpp"

#include <atomic>
#include <cstddef>
#include <string>

namespace
{
/*
 * Stand in for the cabinet's monitoring daemon, which drains
 * /Sys.Monitor.Command.  Without a reader the queue fills and mq_send() answers
 * EAGAIN; nothing the title needs depends on the contents.
 */
void es1MonitorCommandConsumer(const char *, const void *, size_t length, unsigned)
{
    static std::atomic<unsigned long> messages{0};
    const unsigned long seen = messages.fetch_add(1) + 1;
    if (seen == 1)
        log_info("System ES1: draining /Sys.Monitor.Command for the absent monitoring daemon "
                 "(first message %zu bytes)", length);
}
} // namespace

extern "C" int es1PrepareLoad(const char *elfPath)
{
    return es1PrepareTestModeCompat(elfPath);
}

extern "C" int es1DetectGame(const char *elfPath)
{
    return es1SelectTitle(elfPath) ? 1 : 0;
}

extern "C" int es1IsDetected(void)
{
    return es1CurrentTitle() ? 1 : 0;
}

extern "C" int es1InstallHooks(void)
{
    log_info("System ES1: installing ES1-only cabinet and license compatibility hooks");
    ipcSetQueueConsumer("/Sys.Monitor.Command", es1MonitorCommandConsumer);
    es1MagneticCardStart();

    const Es1Title *title = es1CurrentTitle();
    if (!title || !title->installHooks)
        return 0;
    return title->installHooks();
}

extern "C" int es1InstallLateHooks(void)
{
    return 0;
}

extern "C" int es1InitializeGraphics(void)
{
    /* N2 creates the shared context in its own graphics backend. ES1 has no
     * N2 graphics hook, so it must explicitly bring up the common SDL/GL
     * window before pacloader reports GL capabilities. */
    startSDL();
    return 0;
}

extern "C" int es1HandleSystemCommand(const char *command)
{
    if (!command)
        return 0;

    const std::string requested(command);
    const int networkResult = es1HostNetworkCommand(command);
    if (networkResult >= 0)
        return networkResult;

    /* Cabinet bootstraps launch helper daemons. They are not gameplay
     * dependencies on a host loader, and must not be forwarded to Windows. */
    if (requested.rfind("su ", 0) == 0 || requested.rfind("sudo ", 0) == 0 ||
        requested.find("arping.sh") != std::string::npos ||
        requested.find("pinger.pl") != std::string::npos ||
        requested.find("killall perl") != std::string::npos ||
        requested.find("/proc/sys/net/") != std::string::npos)
        return 0;
    return -1;
}

extern "C" const char *es1GetGameTitle(void)
{
    const Es1Title *title = es1CurrentTitle();
    return title ? title->title : "";
}

extern "C" const char *es1GetGameShortTitle(void)
{
    const Es1Title *title = es1CurrentTitle();
    return title ? title->shortTitle : "";
}

extern "C" const char *es1GetGameId(void)
{
    const Es1Title *title = es1CurrentTitle();
    return title ? title->id : "";
}

extern "C" const char *es1GetRevision(void)
{
    const Es1Title *title = es1CurrentTitle();
    return title ? title->revision : "";
}
