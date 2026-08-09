#pragma once

#include "../../../config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Traits of the running title that code outside it needs to know, so that
 * generic layers ask what the title is like rather than which title it is.
 */
typedef struct N2TitleQuirks
{
    /*
     * The cabinet's JVIO board, kickback board and shifter belong to this
     * title.  n2Jvio and n2Kickback stay silent without it, and the loader
     * leaves the game's own input path alone.
     */
    int hasWanganCabinetIo;

    /*
     * The engine expects the loader to own the window: n2InitializeGraphics()
     * reports it has to create one when the GL entry points were not patchable.
     */
    int needsLoaderOwnedWindow;

    /*
     * The title's network code wants the host's own IPv4 reported to the guest
     * rather than a synthesised address.
     */
    int reportsHostIPv4;
} N2TitleQuirks;

/*
 * One System N2 title.  detect() is given the ELF path because not every title
 * can be recognised from its symbols - CS Neo ships a stripped launcher.  Any
 * hook other than detect may be NULL.
 */
typedef struct N2Title
{
    const char *id;
    const char *shortTitle;
    const char *title;
    const char *releaseYear;
    const char *nativeResolutions;
    GameGroup group;
    GameType type;
    int width;
    int height;
    int (*detect)(const char *elfPath);
    int (*installHooks)(void);
    int (*handleSystemCommand)(const char *command);
    int (*handleHostKey)(int key, unsigned int modifiers);
    /* Whether the loader should copy the frame this vsync; NULL means always. */
    int (*shouldBlit)(void);
    N2TitleQuirks quirks;
} N2Title;

/* The detected title, or NULL before n2DetectGame() has recognised one. */
const N2Title *n2CurrentTitle(void);

/* Never NULL: neutral defaults when no title is detected. */
const N2TitleQuirks *n2TitleQuirks(void);

/* True when the detected title has this id.  A title's own module uses it to
 * stay inert while a different title is running. */
int n2TitleIs(const char *id);

/* Walks the title table and records the first match; n2DetectGame() calls it. */
const N2Title *n2SelectTitle(const char *elfPath);

/*
 * Revision string read out of the game at detection time.  The Wangan titles
 * carry it in gRomInfo and the cabinet prints it, so a title publishes what it
 * found here rather than the table hard-coding it.
 */
void n2SetDetectedRevision(const char *revision);
const char *n2DetectedRevision(void);

#ifdef __cplusplus
}
#endif
