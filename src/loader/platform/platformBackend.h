#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
int platformRaiseNativeWindow(void *nativeWindow);
void platformRememberWindowPosition(void *nativeWindow);

#ifdef __cplusplus
}
#endif
