#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Point the reader at the System ES1 terminal cabinet: /dev/ttyS1 and the
 * [NamcoES1] YaCardEmu settings instead of N2's /dev/ttyM2 and [NamcoN2].
 * Call before the title opens the device.
 */
void n2CardReaderUseEs1Terminal(void);

/* Connect to the reader now rather than on the first open(). */
void n2CardReaderStart(void);

int n2CardReaderOpen(const char *path, int flags);
int n2CardReaderIsConnected(void);
const char *n2CardReaderConnectionText(void);
void n2CardReaderRequestInsert(void);
void n2CardReaderRequestEject(void);
void n2CardReaderRegisterCardControl(void);
void n2CardReaderLogDiagnostics(void);
int n2CardReaderIsDescriptor(int fd);

/*
 * Bytes the reader has ready for the game, so poll()/select() can report the
 * descriptor as readable.  Returns 0 when nothing is pending or the pipe is
 * down; the descriptor is always writable.
 */
int n2CardReaderBytesAvailable(int fd);
int n2CardReaderRead(int fd, void *buffer, size_t count);
int n2CardReaderWrite(int fd, const void *buffer, size_t count);
int n2CardReaderClose(int fd);
int n2CardReaderIoctl(int fd, unsigned long request, void *argument);

#ifdef __cplusplus
}
#endif
