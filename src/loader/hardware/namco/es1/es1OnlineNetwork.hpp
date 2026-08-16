#pragma once

/* The shared ES1 online environment: the hostnames a title dials, where
 * [NamcoES1] DNS_* sends them, and the certificate and TLS setup it needs. */

#ifdef __cplusplus
extern "C" {
#endif

/* Maps a hostname onto its configured address, or returns it unchanged. */
const char *es1RedirectedHost(const char *name);

/* Absolute path to the CA bundle the title itself verifies against, or NULL.
 * A private server is trusted by adding its root to that file. */
const char *es1CaCertificatePath(void);

/* libcurl entry points, passed in so the DLL stays owned by the bridge. */
typedef int (*Es1CurlSetopt)(void *handle, int option, ...);
typedef void *(*Es1CurlSlistAppend)(void *list, const char *entry);

/* Applies the CA bundle, address pins and security level to a fresh handle,
 * before the guest's own options so those still win. */
void es1ConfigureCurlHandle(void *handle, Es1CurlSetopt setopt,
                            Es1CurlSlistAppend slistAppend);

#ifdef __cplusplus
}
#endif
