#pragma once

/* WMMT4-only network diagnostics; the shared online setup lives in
 * es1OnlineNetwork.hpp. */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional runtime tracing of the title's own TLS engine, enabled by
 * LL_WMMT4_TLS_TRACE=1.  Nothing is hooked without it.
 */
void wmmt4InstallNetworkDiagnostics(void);

#ifdef __cplusplus
}
#endif
