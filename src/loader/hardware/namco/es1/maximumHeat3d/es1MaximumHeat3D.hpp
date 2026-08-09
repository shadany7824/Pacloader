#pragma once

/* Maximum Heat 3D: no board emulation beyond the shared ES1 layer, but it needs
 * a virtual HASP dongle, a clamp on its audio volume tables, and the legacy
 * clSystemN2 bootstrap talked out of its E30 error. */

#ifdef __cplusplus
extern "C" {
#endif

int es1MaximumHeat3DDetect(const char *elfPath);
int es1MaximumHeat3DInstallHooks(void);

#ifdef __cplusplus
}
#endif
