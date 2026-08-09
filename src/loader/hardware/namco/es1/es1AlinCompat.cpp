#include "es1AlinCompat.h"

#include "../../../log/log.h"

#include <cstdint>
#include <cstring>

namespace
{
constexpr uintptr_t Es1AlinHandleValue = 0x45534111u;
void *const Es1AlinHandle = reinterpret_cast<void *>(Es1AlinHandleValue);

bool isAlin(const char *filename)
{
    if (!filename)
        return false;

    const char *slash = std::strrchr(filename, '/');
    const char *backslash = std::strrchr(filename, '\\');
    const char *basename = filename;
    if (slash && slash + 1 > basename)
        basename = slash + 1;
    if (backslash && backslash + 1 > basename)
        basename = backslash + 1;
    return std::strcmp(basename, "alin.dll") == 0;
}

extern "C" int es1AlinInit()
{
    return 0;
}

extern "C" void es1AlinTerm()
{
}

extern "C" int es1AlinPad(int, void *)
{
    return 0;
}

extern "C" int es1AlinKeyboard(int, void *)
{
    return 0;
}

extern "C" int es1AlinMouse(int, void *)
{
    return 0;
}
}

extern "C" int es1AlinDlopen(const char *filename, void **handle)
{
    if (!isAlin(filename))
        return 0;

    if (handle)
        *handle = Es1AlinHandle;
    log_debug("System ES1 input: virtualized %s", filename);
    return 1;
}

extern "C" void *es1AlinDlsym(void *handle, const char *symbol)
{
    if (handle != Es1AlinHandle || !symbol)
        return nullptr;

    if (std::strcmp(symbol, "alin_init") == 0)
        return reinterpret_cast<void *>(es1AlinInit);
    if (std::strcmp(symbol, "alin_term") == 0)
        return reinterpret_cast<void *>(es1AlinTerm);
    if (std::strcmp(symbol, "alin_pad") == 0)
        return reinterpret_cast<void *>(es1AlinPad);
    if (std::strcmp(symbol, "alin_keyboard") == 0)
        return reinterpret_cast<void *>(es1AlinKeyboard);
    if (std::strcmp(symbol, "alin_mouse") == 0)
        return reinterpret_cast<void *>(es1AlinMouse);

    return nullptr;
}

extern "C" int es1AlinDlclose(void *handle)
{
    return handle == Es1AlinHandle ? 0 : -1;
}
