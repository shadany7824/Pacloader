#pragma once

/*
 * /dev/urandom and friends.  Linux programs assume these always exist; OpenSSL
 * seeds its PRNG from them, and an unseeded PRNG makes ssl23_client_hello()
 * fail with an empty error queue, so TLS connects die silently.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the random devices with the virtual device registry. */
void registerRandomDevices(void);

/* True when the descriptor came from one of those devices. */
int randomDeviceOwnsDescriptor(int fd);

#ifdef __cplusplus
}
#endif
