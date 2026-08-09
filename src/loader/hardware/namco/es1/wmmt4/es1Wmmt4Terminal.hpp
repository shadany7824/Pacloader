#pragma once

/* Built-in terminal cabinet.  Online play needs one on the cabinet LAN and only
 * one loader instance runs, so a thread multicasts the terminal's periodic
 * message onto the group the drive cabinet listens to. */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Starts the emulator once, if the configuration asks for it and this instance
 * is a drive cabinet.  heartbeatSerial is the dongle serial the heartbeat
 * presents; LL_WMMT4_TERMINAL_SERIAL overrides it.
 */
void wmmt4StartTerminalEmulator(const char *heartbeatSerial);

#ifdef __cplusplus
}
#endif
