#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ES1 steering PCB is separate from its NA-JV I/O board. */
bool es1KickbackClaimsPath(const char *path);
int es1KickbackOpen(const char *path, int flags);
int es1KickbackOwnsDescriptor(int fd);
int es1KickbackBytesAvailable(int fd);
int es1KickbackRead(int fd, void *buffer, size_t count);
int es1KickbackWrite(int fd, const void *buffer, size_t count);
int es1KickbackClose(int fd);
int es1KickbackIoctl(int fd, unsigned long request, void *argument);

/* WMMT4's STR PCB volunteers these reports instead of receiving a request. */
void es1KickbackReportSelfCheck(void);
void es1KickbackReportMotorPower(int running);
/* Power-on reply with the self-check result attached, for a board the title
 * never asks for that result separately. */
void es1KickbackReportPoweredSelfCheck(void);

#ifdef __cplusplus
}
#endif
