#pragma once

/* WMMT4 FFB hooks forward title-side values to the shared SDL backend. */

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the FFB hooks; returns how many were installed. */
int wmmt4InstallFfbHooks(void);

#ifdef __cplusplus
}
#endif
