#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Answers the host-level commands every N2 cabinet issues.  Returns the shell
 * exit status, or -1 when the command is not one of them.
 */
int n2HandleHostSystemCommand(const char *command);

#ifdef __cplusplus
}
#endif
