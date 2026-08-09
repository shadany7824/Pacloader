#pragma once

/*
 * WMMT4's online environment: the hostnames it dials, where [NamcoES1] DNS_*
 * sends them, and the certificate and TLS setup its libcurl side expects.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maps a hostname the title asks for onto the address configured for it.
 * Returns `name` unchanged when the title is not WMMT4, when the name is not
 * one of its services, or when that service has no configured target.
 */
const char *wmmt4RedirectedHost(const char *name);

/*
 * Absolute path to the CA bundle the title itself verifies against
 * (data/Network/certs/v388-ca-cert.pem), or NULL when it cannot be found.
 * A private server is trusted by adding its root to that file.
 */
const char *wmmt4CaCertificatePath(void);

/* libcurl entry points, passed in so the DLL stays owned by the bridge. */
typedef int (*Wmmt4CurlSetopt)(void *handle, int option, ...);
typedef void *(*Wmmt4CurlSlistAppend)(void *list, const char *entry);

/* Applies the title's CA bundle, address pins and security level to a fresh
 * easy handle.  Called before the guest's own options, so those still win. */
void wmmt4ConfigureCurlHandle(void *handle, Wmmt4CurlSetopt setopt,
                              Wmmt4CurlSlistAppend slistAppend);

/*
 * Optional runtime tracing of the title's own TLS engine, enabled by
 * LL_WMMT4_TLS_TRACE=1.  Nothing is hooked without it.
 */
void wmmt4InstallNetworkDiagnostics(void);

#ifdef __cplusplus
}
#endif
