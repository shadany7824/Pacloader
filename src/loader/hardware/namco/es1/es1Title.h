#pragma once

#include "../../../config/config.h"
#include "../../../platform/platformBackend.h"

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
     * The cabinet's screen is a fixed panel, so the window neither resizes nor
     * takes the legacy resize nudge, and it is centred when it is not
     * fullscreen.
     */
    int fixedWindowSize;

    /*
     * The cabinet has an H-pattern shifter whose gears each close two JVS
     * switch bits, so the input layer applies it as a unit each frame.
     */
    int hasHPatternShifter;

    /* The card is a reader command rather than a persistent JVS switch. */
    int cardInsertIsCommand;

    /* The cabinet's TEST switch latches, so holding a key is not how it works. */
    int testSwitchIsLatching;

    /*
     * The title pumps SDL from two paths, so an axis event can be consumed by
     * one before the other flushes it; the bound axis is polled every frame as
     * a second source of truth.
     */
    int pollsSteeringAxisEachFrame;

    /* The guest is told it runs windowed through __NU_SCREEN_WINDOWED. */
    int reportsWindowedScreen;

    /*
     * The pedals are reported over JVS shifted left six bits, so the value the
     * title displays matches the cabinet. 0 uses the generic maximum.
     */
    int jvsPedalMaximum;

    /* Which control panel the input layer should map; see CabinetPanel. */
    int cabinetPanel;

    /*
     * The steering board volunteers its state instead of waiting to be asked,
     * so the emulated one offers motor and self-check replies unprompted.
     */
    int steeringBoardReportsUnprompted;

    /* Whether the board's self-check result arrives unasked in the position
     * stream, rather than only when the title requests it. */
    int steeringBoardVolunteersSelfCheck;

    /*
     * What the loader's JVS board calls itself, or NULL for the ES1 default.
     * A title's JVIO master checks this before it will talk to the board: WMMT4
     * requires the string to contain "NA-JV".
     */
    const char *jvsBoardIdent;

    /* What the cabinet's I/O board declares when the title enumerates it.
     * Leave declared at 0 to keep the generic ES1 profile. */
    struct
    {
        int declared;
        int players;
        int switches;
        int coins;
        int analogueInChannels;
        int analogueInBits;
        int rotaryChannels;
        int keypad;
        int generalPurposeOutputs;
        int analogueOutChannels;
        int displayOutColumns;
        int displayOutRows;
    } jvsBoard;
} Es1TitleQuirks;

/*
 * One System ES1 title.  detect() inspects the game folder and returns 1 when
 * it recognises its own title; installHooks() may be NULL for a title that
 * needs nothing beyond the shared ES1 layer.
 */
typedef struct Es1Title
{
    const char *id;
    /* controls.ini section for this cabinet, or NULL to use the game type. */
    const char *controlsProfileName;
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
