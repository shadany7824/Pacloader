#if defined(_WIN32) || defined(__MINGW32__)

#include "randomDevice.hpp"

#include "virtualDeviceRegistry.hpp"
#include "../log/log.h"

#include <array>
#include <cstring>
#include <mutex>
#include <random>

#include <errno.h>
#include <windows.h>

namespace
{
/* Well clear of the descriptors the hardware devices hand out. */
constexpr int FirstDescriptor = 0x5210;
constexpr size_t MaxOpen = 8;

struct Slot
{
    bool used = false;
};

std::array<Slot, MaxOpen> slots{};
std::mutex slotsMutex;

/* RtlGenRandom is the CSPRNG every Windows CRT uses underneath.  A Mersenne
 * Twister stands in if it is unavailable: worse entropy, but better than a
 * caller concluding the machine has no random source at all. */
typedef BOOLEAN(WINAPI *RtlGenRandomFn)(PVOID buffer, ULONG length);

RtlGenRandomFn systemRandom()
{
    static const RtlGenRandomFn function = [] {
        const HMODULE module = LoadLibraryA("advapi32.dll");
        return module ? reinterpret_cast<RtlGenRandomFn>(
                            reinterpret_cast<void *>(GetProcAddress(module, "SystemFunction036")))
                      : nullptr;
    }();
    return function;
}

void fillRandom(unsigned char *buffer, size_t count)
{
    if (const RtlGenRandomFn generate = systemRandom())
    {
        if (generate(buffer, static_cast<ULONG>(count)))
            return;
    }

    static std::mt19937 fallback{std::random_device{}()};
    static std::mutex fallbackMutex;
    std::lock_guard<std::mutex> lock(fallbackMutex);
    for (size_t i = 0; i < count; ++i)
        buffer[i] = static_cast<unsigned char>(fallback() & 0xff);
}

bool claimsPath(const char *path)
{
    if (!path)
        return false;
    static const char *const names[] = {"/dev/urandom", "/dev/random", "/dev/srandom",
                                        "/dev/arandom", "/dev/prandom", "/dev/hwrng"};
    for (const char *name : names)
        if (std::strcmp(path, name) == 0)
            return true;
    return false;
}

int openDevice(const char *path, int flags)
{
    (void)flags;
    std::lock_guard<std::mutex> lock(slotsMutex);
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].used)
            continue;
        slots[i].used = true;
        log_debug("Random device: %s -> fd %d", path, FirstDescriptor + static_cast<int>(i));
        return FirstDescriptor + static_cast<int>(i);
    }
    errno = EMFILE;
    return -1;
}

int ownsDescriptor(int fd)
{
    const int index = fd - FirstDescriptor;
    if (index < 0 || static_cast<size_t>(index) >= slots.size())
        return 0;
    std::lock_guard<std::mutex> lock(slotsMutex);
    return slots[static_cast<size_t>(index)].used ? 1 : 0;
}

/* Never blocks, so poll() must always report it readable. */
int bytesAvailable(int)
{
    return 4096;
}

int readDevice(int fd, void *buffer, size_t count)
{
    if (!ownsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }
    if (!buffer)
    {
        errno = EFAULT;
        return -1;
    }
    if (count == 0)
        return 0;

    fillRandom(static_cast<unsigned char *>(buffer), count);
    return static_cast<int>(count);
}

/* Seeding the pool back is a privileged no-op on Linux too; accept it. */
int writeDevice(int fd, const void *, size_t count)
{
    if (!ownsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }
    return static_cast<int>(count);
}

int closeDevice(int fd)
{
    const int index = fd - FirstDescriptor;
    if (index < 0 || static_cast<size_t>(index) >= slots.size())
    {
        errno = EBADF;
        return -1;
    }
    std::lock_guard<std::mutex> lock(slotsMutex);
    slots[static_cast<size_t>(index)].used = false;
    return 0;
}

int ioctlDevice(int fd, unsigned long request, void *argument)
{
    if (!ownsDescriptor(fd))
    {
        errno = EBADF;
        return -1;
    }
    /* RNDGETENTCNT: callers only ever check that the pool is not empty. */
    if (request == 0x80045200ul && argument)
    {
        *static_cast<int *>(argument) = 4096;
        return 0;
    }
    return 0;
}
} // namespace

extern "C" void registerRandomDevices(void)
{
    const VirtualDeviceRegistry::Device device{
        "random (/dev/urandom, /dev/random)",
        claimsPath,
        openDevice,
        ownsDescriptor,
        bytesAvailable,
        readDevice,
        writeDevice,
        closeDevice,
        ioctlDevice,
        nullptr};
    VirtualDeviceRegistry::registerDevice(device);
}

extern "C" int randomDeviceOwnsDescriptor(int fd)
{
    return ownsDescriptor(fd);
}

#endif
