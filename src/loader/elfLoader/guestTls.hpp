#pragma once

#include <cstdint>

namespace GuestTls
{
/* Install a Linux i386-compatible GS segment for the calling host thread. */
bool InstallForCurrentThread();

bool IsInstalled();

/* Modern WOW64 does not permit the Linux GS selector to remain loaded.  This
 * reports that GS memory instructions are handled by the software fallback. */
bool UsesFsOverride();
void EnterHostCall();
void EnterGuestCode();

class HostCallScope
{
  public:
    HostCallScope();
    ~HostCallScope();

    HostCallScope(const HostCallScope &) = delete;
    HostCallScope &operator=(const HostCallScope &) = delete;
};

/* Build an ABI-neutral i386 trampoline for host functions called by guest
 * code. It switches to the Windows GS state for the host call and restores
 * the guest state without changing EAX/EDX return values or stack cleanup. */
void *WrapHostFunction(void *target);

/* Implementation of the i386 ELF TLS resolver used by libgcc/glibc. */
void *GetAddress(const void *tlsIndex);
}
