#pragma once

/* Banapassport IC card reader.  Sys::Device::IcCard drives a statically linked
 * reader library, and with no reader present the boot check stops with E0712
 * (drive) / E0702 (terminal), so these hooks answer for one. */

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the reader hooks; returns how many were installed. */
int wmmt4InstallCardHooks(void);

/* Present and remove with banapassportPresent()/banapassportRemove() in
 * hardware/namco/banapassport - the same for every title using this reader. */

#ifdef __cplusplus
}
#endif
