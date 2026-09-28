#pragma once

/* Title ids in the ES1 title table; the WMMT5 modules test against them. */
#define ES1_TITLE_ID_WMMT5DXP "WMMT5DX+"
#define ES1_TITLE_ID_WMMT5DXP_CHINA "WMMT5DX+_CHN"
#define ES1_TITLE_ID_WMMT5DX_CHINA "WMMT5DX_CHN"

#ifdef __cplusplus
extern "C" {
#endif

int es1Wmmt5dxPlusDetect(const char *elfPath);
int es1Wmmt5dxPlusChinaDetect(const char *elfPath);
int es1Wmmt5dxChinaDetect(const char *elfPath);
int es1Wmmt5InstallHooks(void);

#ifdef __cplusplus
}
#endif
