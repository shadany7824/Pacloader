#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// True for the Wangan Midnight titles, which share an engine and a cabinet
int n2IsWanganTitle(void);

// The path is needed because not every N2 title can be recognised from its
// symbols: CSNeo executable is a stripped launcher.
int n2PrepareLoad(const char *elfPath);
int n2DetectGame(const char *elfPath);
int n2IsDetected(void);
int n2InstallHooks(void);
int n2InstallAdmHooks(void);
int n2InstallLateTextureHooks(void);
int n2InitializeGraphics(void);
int n2HandleSystemCommand(const char *command);
// Handles N2-specific host hotkeys before SDL 1.2 passes them to the game.
int n2HandleHostKey(int key, uint32_t modifiers);
/* Runs the sequential shifter from the GearUp/GearDown bindings and reports the
 * gear, or 0 when the raw shifter switches are in use.  n2GearSwitchBits()
 * turns that gear into the JVS switch bits the cabinet is wired to. */
int n2UpdateShifter(void);
uint16_t n2GearSwitchBits(int gear);

/*
 * Raw count the cabinet's wheel or pedal potentiometer reports for a 0..1
 * position, as both the direct-write path and the JVS bridge have to publish
 * the same reading.
 */
enum
{
    N2_ANALOGUE_STEERING = 0,
    N2_ANALOGUE_ACCELERATOR = 1,
    N2_ANALOGUE_BRAKE = 2
};
uint16_t n2AnalogueCount(int channel, float normalized);

const char *n2GetGameTitle(void);
const char *n2GetGameShortTitle(void);
const char *n2GetGameId(void);
const char *n2GetRevision(void);

// Decide whether WMMT should copy the frame.
int n2WmmtShouldBlit(void);

#ifdef __cplusplus
}
#endif
