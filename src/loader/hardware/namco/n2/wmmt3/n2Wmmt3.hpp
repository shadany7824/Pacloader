#pragma once

/* The WMMT3 family.  3, 3DX and 3DX+ share one engine and cabinet and differ
 * only in the revision string gRomInfo carries, so they are four rows in the
 * title table over a single module. */

/* Title ids in the N2 title table. */
#define N2_TITLE_ID_WMMT3 "WMMT3"
#define N2_TITLE_ID_WMMT3DX "WMMT3DX"
#define N2_TITLE_ID_WMMT3DX_PLUS "WMMT3DX+"
#define N2_TITLE_ID_WMMT3_FAMILY "WMMT3-SERIES"

#ifdef __cplusplus
extern "C" {
#endif

int n2Wmmt3Detect(const char *elfPath);
int n2Wmmt3dxDetect(const char *elfPath);
int n2Wmmt3dxPlusDetect(const char *elfPath);
int n2Wmmt3FamilyDetect(const char *elfPath);

int n2Wmmt3InstallHooks(void);
int n2Wmmt3HandleSystemCommand(const char *command);

/* Whether the loader should copy the frame this vsync. */
int n2Wmmt3ShouldBlit(void);

#ifdef __cplusplus
}
#endif
