#pragma once

#include "../../../config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Traits of the running title that code outside it needs, so generic layers ask
 * what the title is like rather than which title it is.  A new title adds a row
 * here instead of another branch in the bridges. */
typedef struct Es1TitleQuirks
{
    /*
     * Address and mask reported for the guest's virtual eth0.  A zero address
     * means report whatever the host adapter has.  WMMT4 wants a fixed private
     * address because its cabinet LAN discovery compares against it.
     */
    unsigned char eth0Address[4];
    unsigned char eth0Mask[4];

    /*
     * Serial port the steering/kickback board answers on, or NULL when this
     * title has no board there.  The layout is per-title: WMMT4 puts its IC
     * card reader on the port Maximum Heat 3D uses for steering.
     */
    const char *kickbackDevicePath;

    /*
     * The title's directory enumerator has no end-of-directory branch and
     * re-enters its loop body with the null entry unless readdir() reports an
     * error, so end of directory has to be signalled as EBADF.
     */
    int readdirEndOfDirectoryIsError;

    /*
     * What the loader's JVS board calls itself, or NULL for the ES1 default.
     * A title's JVIO master checks this before it will talk to the board: WMMT4
     * requires the string to contain "NA-JV".
     */
    const char *jvsBoardIdent;
} Es1TitleQuirks;

/*
 * One System ES1 title.  detect() inspects the game folder and returns 1 when
 * it recognises its own title; installHooks() may be NULL for a title that
 * needs nothing beyond the shared ES1 layer.
 */
typedef struct Es1Title
{
    const char *id;
    const char *shortTitle;
    const char *title;
    const char *revision;
    const char *releaseYear;
    GameRegion region;
    GameGroup group;
    int (*detect)(const char *elfPath);
    int (*installHooks)(void);
    Es1TitleQuirks quirks;
} Es1Title;

/* The detected title, or NULL before es1DetectGame() has recognised one. */
const Es1Title *es1CurrentTitle(void);

/* True when the detected title has this id.  A title's own module uses it to
 * stay inert while a different title is running. */
int es1TitleIs(const char *id);

/* Walks the title table and records the first match; es1DetectGame() calls it. */
const Es1Title *es1SelectTitle(const char *elfPath);

/* The build the detected package reports, or "" when it carries none. The
 * address tables in the title modules only hold for one build, so it is worth
 * recording what was actually found. */
void es1SetDetectedRevision(const char *revision);
const char *es1DetectedRevision(void);

/* Never NULL: falls back to neutral defaults when no title is detected. */
const Es1TitleQuirks *es1TitleQuirks(void);

#ifdef __cplusplus
}
#endif
