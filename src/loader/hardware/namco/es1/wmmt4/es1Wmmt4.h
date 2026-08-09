#pragma once

/* Title id in the ES1 title table; the WMMT4 modules test against it. */
#define ES1_TITLE_ID_WMMT4 "WMMT4"

#ifdef __cplusplus
extern "C" {
#endif

int es1Wmmt4Detect(const char *elfPath);
int es1Wmmt4InstallHooks(void);

#ifdef __cplusplus
}
#endif
