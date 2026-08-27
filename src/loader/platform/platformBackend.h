#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The cabinet's control panel. The JVS switch map and the default bindings
 * belong to the input layer, but which set to use is a property of the cabinet,
 * so a title declares its panel instead of the input layer naming the title. A
 * later game on the same cabinet reuses the value unchanged.
 */
typedef enum
{
    CABINET_PANEL_GENERIC = 0,
    CABINET_PANEL_WANGAN_N2,
    CABINET_PANEL_WANGAN_ES1,
    CABINET_PANEL_MAXIMUM_HEAT_3D,
    CABINET_PANEL_KIZUNA_POD
} CabinetPanel;


int platformPrepareLoad(const char *elfPath);
int platformDetectGame(const char *elfPath);
int platformIsDetected(void);
int platformIsN2(void);
int platformIsES1(void);
int platformInstallHooks(void);
int platformInstallAdmHooks(void);
int platformInstallLateHooks(void);
int platformInitializeGraphics(void);
int platformHandleSystemCommand(const char *command);
int platformHandleHostKey(int key, uint32_t modifiers);
int platformHandleHostKeyEvent(int key, uint32_t modifiers, int pressed);
int platformWantsCabinetArgument(void);
const char *platformName(void);
void platformRegisterVirtualDevices(void);
void platformRegisterCardControl(void);
/* Traits the generic layers ask about, so they need not name a title. */
int platformWindowIsFixedSize(void);
int platformHasHPatternShifter(void);
int platformCardInsertIsCommand(void);
int platformTestSwitchIsLatching(void);
int platformPollsSteeringAxisEachFrame(void);
int platformReportsWindowedScreen(void);
/* 0 when the generic analogue maximum applies. */
int platformJvsPedalMaximum(void);
/* NULL when the game type decides the controls.ini section. */
const char *platformControlsProfileName(void);
int platformCabinetPanel(void);

int platformRaiseNativeWindow(void *nativeWindow);
void platformRememberWindowPosition(void *nativeWindow);

#ifdef __cplusplus
}
#endif
