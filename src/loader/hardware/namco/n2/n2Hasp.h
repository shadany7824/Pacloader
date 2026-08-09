#pragma once

/*
 * Virtual USB dongle shared by the N2 titles: a checksum-protected 0xD40 image
 * plus the clHasp/clHasp2 entry points the cabinet reads it through.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Reserves the virtual HASP entry points before a dependent ELF is relocated.
void n2RegisterHaspPreloadOverrides(void);

// Hooks the dongle API in the loaded ELF.  Returns 0 on success.
int n2HaspInstallHooks(void);

#ifdef __cplusplus
}
#endif
