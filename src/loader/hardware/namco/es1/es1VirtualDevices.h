#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void es1RegisterVirtualDevices(void);

/* The magnetic card reader exists only on the terminal cabinet. */
int es1MagneticCardEnabled(void);
void es1MagneticCardStart(void);

#ifdef __cplusplus
}
#endif
