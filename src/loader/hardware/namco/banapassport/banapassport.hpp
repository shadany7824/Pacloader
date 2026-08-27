#pragma once

#include <cstddef>
#include <cstdint>

/* Banapassport / Amusement IC card reader, shared by every title that drives
 * one. A title supplies its own hook addresses and shims; the card lives here. */

#ifdef __cplusplus
extern "C" {
#endif

/* Matches OpenBanapass's rawCardData[168]. */
#define BANAPASSPORT_RECORD_SIZE 168

/* Result codes from the library's own checks, so shims can answer as it does. */
#define BANAPASSPORT_OK 1
#define BANAPASSPORT_BAD_DEVICE (-100)
#define BANAPASSPORT_NOT_INITIALISED (-101)
#define BANAPASSPORT_NO_CARD (-102)
#define BANAPASSPORT_STATUS_OK 0

/* Readers the library accepts, from its range check on argument zero. */
#define BANAPASSPORT_MAX_DEVICES 2

/* A hook that returns a result without invoking these leaves the title's state
 * machine waiting on its ack forever. */
typedef void (*BanapassportCommandCallback)(int deviceId, int status, void *userData);
typedef void (*BanapassportTouchCallback)(int deviceId, int status, void *cardRecord,
                                          void *userData);

int banapassportBadDevice(int deviceId);

/* Run a command's completion handler as if the reader had answered at once. */
void banapassportComplete(BanapassportCommandCallback callback, int deviceId, void *userData);

/* Completes at once when a card is on the reader, otherwise stays pending until
 * one is presented. `autoInsert` presents one as the request arrives. */
int banapassportWaitTouch(int deviceId, BanapassportTouchCallback callback, void *userData,
                          int autoInsert);

/* Presenting completes a pending wait-touch; null or empty strings present
 * whichever card is already loaded. */
void banapassportPresent(const char *chipId, const char *accessCode);
void banapassportRemove(void);

/* For titles whose entry points write into a caller block instead of using a
 * completion callback. */
const uint8_t *banapassportRecord(void);
int banapassportCardOnReader(void);

/* Writes the legacy NBGIC header into a WMMT4 response buffer. */
int banapassportWriteNbgicHeader(void *buffer);

/* Loads the reader's own file once, creating it with commented defaults if
 * absent; null uses "banapassport.ini". [Reader] ENABLED/AUTO_INSERT/
 * DIAGNOSTICS, [Card] ChipId (32 hex: IDm then PMm) / AccessCode (20 digits). */
void banapassportConfigure(const char *file);

int banapassportReaderEnabled(void);
int banapassportAutoInsert(void);
int banapassportDiagnostics(void);

/* Registers with the shared card-control layer, so insert/eject and the
 * on-screen reader status work as they do for the N2 magnetic reader. */
void banapassportRegisterCardControl(const char *name);

#ifdef __cplusplus
}
#endif
