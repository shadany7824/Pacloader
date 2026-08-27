#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Kidou Senshi Gundam: Senjou no Kizuna, the System ES1 revisions.  Only the
 * station (the POD the player sits in) is wired up; the terminal ships as a
 * separate executable from the same package and will want its own row. */
int es1KizunaDetect(const char *elfPath);

/* Registry interposition has to be in place before the ELF is relocated,
 * because that is when libarcaderegistry's own exports get bound. */
int es1KizunaPrepareLoad(void);
int es1KizunaInstallHooks(void);

#ifdef __cplusplus
}
#endif
