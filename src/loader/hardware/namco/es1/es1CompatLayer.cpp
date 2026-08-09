#include "es1CompatLayer.h"

#include "../../../config/config.h"
#include "../../../input/sdlInput.h"
#include "../../../log/log.h"
#include "../../../../minhook/include/MinHook.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>
#include <windows.h>

namespace
{
using ZByte = unsigned char;
using ZUInt = unsigned int;
using ZULong = unsigned long;
using ZAlloc = void *(*)(void *, ZUInt, ZUInt);
using ZFree = void (*)(void *, void *);

struct Es1ZStream
{
    ZByte *nextIn;
    ZUInt availableIn;
    ZULong totalIn;
    ZByte *nextOut;
    ZUInt availableOut;
    ZULong totalOut;
    char *message;
    void *state;
    ZAlloc allocate;
    ZFree free;
    void *opaque;
    int dataType;
    ZULong checksum;
    ZULong reserved;
};

using ZlibVersion = const char *(*)();
using InflateInit2 = int (*)(Es1ZStream *, int, const char *, int);
using Inflate = int (*)(Es1ZStream *, int);
using InflateEnd = int (*)(Es1ZStream *);

struct Es1ZlibApi
{
    HMODULE module = nullptr;
    ZlibVersion version = nullptr;
    InflateInit2 initialize = nullptr;
    Inflate inflate = nullptr;
    InflateEnd finish = nullptr;
};

Es1ZlibApi &zlibApi()
{
    static Es1ZlibApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        api.module = LoadLibraryA("zlib1.dll");
        if (!api.module)
        {
            log_error("System ES1: zlib1.dll is unavailable; gzip resources cannot be loaded");
            return;
        }
        api.version = reinterpret_cast<ZlibVersion>(
            reinterpret_cast<void *>(GetProcAddress(api.module, "zlibVersion")));
        api.initialize = reinterpret_cast<InflateInit2>(
            reinterpret_cast<void *>(GetProcAddress(api.module, "inflateInit2_")));
        api.inflate = reinterpret_cast<Inflate>(
            reinterpret_cast<void *>(GetProcAddress(api.module, "inflate")));
        api.finish = reinterpret_cast<InflateEnd>(
            reinterpret_cast<void *>(GetProcAddress(api.module, "inflateEnd")));
        if (!api.version || !api.initialize || !api.inflate || !api.finish)
        {
            log_error("System ES1: zlib1.dll does not export the required inflate API");
            FreeLibrary(api.module);
            api = {};
        }
    });
    return api;
}

bool addressIsMapped(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT)
        return false;

    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

}

int es1InstallHookTable(const Es1HookSpec *hooks, size_t count, const char *title)
{
    if (!hooks)
        return 0;

    int installed = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const Es1HookSpec &hook = hooks[i];
        if (!addressIsMapped(hook.address))
        {
            log_warn("System ES1 %s: hook target %s at %p is not mapped", title,
                     hook.name, reinterpret_cast<void *>(hook.address));
            continue;
        }

        const MH_STATUS status = MH_CreateHook(reinterpret_cast<void *>(hook.address),
                                               hook.replacement, hook.original);
        if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
        {
            log_error("System ES1 %s: failed to hook %s at %p (MinHook status %d)", title,
                      hook.name, reinterpret_cast<void *>(hook.address),
                      static_cast<int>(status));
            continue;
        }
        log_info("System ES1 %s: hooked %s at %p", title, hook.name,
                 reinterpret_cast<void *>(hook.address));
        ++installed;
    }
    return installed;
}

bool es1CompatGzipDecompress(const uint8_t *data, size_t size,
                             std::vector<uint8_t> &output)
{
    output.clear();
    if (!data || size < 2 || data[0] != 0x1f || data[1] != 0x8b ||
        size > std::numeric_limits<ZUInt>::max())
        return false;

    Es1ZlibApi &api = zlibApi();
    if (!api.module)
        return false;

    Es1ZStream stream{};
    stream.nextIn = const_cast<ZByte *>(data);
    stream.availableIn = static_cast<ZUInt>(size);
    constexpr int GzipWindowBits = 15 + 16;
    if (api.initialize(&stream, GzipWindowBits, api.version(), sizeof(stream)) != 0)
        return false;

    constexpr size_t InitialSize = 64 * 1024;
    constexpr size_t MaximumSize = 512 * 1024 * 1024;
    output.resize(std::max(InitialSize, std::min(size * 4, MaximumSize)));
    bool success = false;
    for (;;)
    {
        if (stream.totalOut >= output.size())
        {
            if (output.size() >= MaximumSize)
                break;
            output.resize(std::min(output.size() * 2, MaximumSize));
        }
        const size_t produced = static_cast<size_t>(stream.totalOut);
        stream.nextOut = output.data() + produced;
        stream.availableOut = static_cast<ZUInt>(
            std::min(output.size() - produced,
                     static_cast<size_t>(std::numeric_limits<ZUInt>::max())));
        const int result = api.inflate(&stream, 0);
        if (result == 1) /* Z_STREAM_END */
        {
            success = true;
            break;
        }
        if (result != 0 || (stream.availableIn == 0 && stream.availableOut != 0))
            break;
    }
    api.finish(&stream);
    if (!success)
    {
        output.clear();
        return false;
    }
    output.resize(stream.totalOut);
    return true;
}

uint32_t es1CompatCrc32Mpeg(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint32_t>(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

int es1CompatReadBlob(const uint8_t *blob, size_t blobSize, int offset, int length,
                      uint8_t *buffer)
{
    if (!blob || !buffer || offset < 0 || length < 0 ||
        static_cast<size_t>(offset) > blobSize ||
        static_cast<size_t>(length) > blobSize - static_cast<size_t>(offset))
        return 1;
    std::memcpy(buffer, blob + offset, static_cast<size_t>(length));
    return 0;
}

int es1CompatWriteBlob(uint8_t *blob, size_t blobSize, int offset, int length,
                       const uint8_t *buffer)
{
    if (!blob || !buffer || offset < 0 || length < 0 ||
        static_cast<size_t>(offset) > blobSize ||
        static_cast<size_t>(length) > blobSize - static_cast<size_t>(offset))
        return 1;
    std::memcpy(blob + offset, buffer, static_cast<size_t>(length));
    return 0;
}
