#include "n2.h"
#include "n2Title.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include "n2SystemCommand.hpp"
#include "csneo/n2CsNeo.h"

#include "../../../log/log.h"

extern "C" int n2PrepareLoad(const char *elfPath)
{
    return n2CsNeoPrepareLoad(elfPath);
}

extern "C" int n2DetectGame(const char *elfPath)
{
    return n2SelectTitle(elfPath) ? 1 : 0;
}

extern "C" int n2IsDetected(void)
{
    return n2CurrentTitle() ? 1 : 0;
}

extern "C" int n2IsWanganTitle(void)
{
    return n2TitleQuirks()->hasWanganCabinetIo;
}

extern "C" int n2InstallHooks(void)
{
    const N2Title *title = n2CurrentTitle();
    if (!title || !title->installHooks)
        return 0;
    return title->installHooks();
}

extern "C" int n2HandleSystemCommand(const char *command)
{
    const N2Title *title = n2CurrentTitle();
    if (!title || !command)
        return -1;

    /* A title gets first refusal, because the paths it asks about are its own
     * data layout; whatever is left is the same for every N2 cabinet. */
    if (title->handleSystemCommand)
    {
        const int handled = title->handleSystemCommand(command);
        if (handled >= 0)
            return handled;
    }
    return n2HandleHostSystemCommand(command);
}

extern "C" int n2HandleHostKey(int key, uint32_t modifiers)
{
    const N2Title *title = n2CurrentTitle();
    if (!title || !title->handleHostKey)
        return 0;
    return title->handleHostKey(key, modifiers);
}

extern "C" int n2WmmtShouldBlit(void)
{
    const N2Title *title = n2CurrentTitle();
    if (!title || !title->shouldBlit)
        return 1;
    return title->shouldBlit();
}

extern "C" const char *n2GetGameTitle(void)
{
    const N2Title *title = n2CurrentTitle();
    return title ? title->title : "";
}

extern "C" const char *n2GetGameShortTitle(void)
{
    const N2Title *title = n2CurrentTitle();
    return title ? title->shortTitle : "";
}

extern "C" const char *n2GetGameId(void)
{
    const char *revision = n2DetectedRevision();
    if (revision && *revision)
        return revision;

    const N2Title *title = n2CurrentTitle();
    return title ? title->id : "NAMCO-N2";
}

extern "C" const char *n2GetRevision(void)
{
    return n2DetectedRevision();
}

#endif
