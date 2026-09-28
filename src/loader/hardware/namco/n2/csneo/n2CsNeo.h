#pragma once

/* Title id in the N2 title table. */
#define N2_TITLE_ID_CSNEO "CSNEO"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CS Neo uses a stripped launcher, so it is identified from its installation
// layout before the ELF and engine module are loaded.
int n2CsNeoLooksLikeGame(const char *elfPath);
int n2CsNeoDetect(const char *elfPath);
int n2CsNeoPrepareLoad(const char *elfPath);

// Installs the title-specific compatibility layer after game detection.
int n2CsNeoInstallHooks(void);

// Arguments the cabinet's .execrc always passes, as NULL-terminated groups:
// a group is appended when its first word is missing from the command line.
const char *const *n2CsNeoRequiredArguments(void);

// Handles CS Neo host shortcuts. Returns non-zero when the key was consumed.
int n2CsNeoHandleHostKey(int key, uint32_t modifiers);

#ifdef __cplusplus
}
#endif
