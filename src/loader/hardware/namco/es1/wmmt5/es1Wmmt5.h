#pragma once

/* Title id in the ES1 title table; the WMMT5 modules test against it. */
#define ES1_TITLE_ID_WMMT5DXP "WMMT5DX+"

#ifdef __cplusplus
extern "C" {
#endif

int es1Wmmt5dxPlusDetect(const char *elfPath);
int es1Wmmt5dxPlusInstallHooks(void);

#ifdef __cplusplus
}
#endif
