#include "n2Graphics.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include "n2.h"
#include "n2Hook.h"
#include "n2Title.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <mutex>

#include <SDL3/SDL.h>

#include "../../../config/config.h"
#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../elfLoader/glHooks.hpp"
#include "../../../graphics/sdlCalls.h"
#include "../../../log/log.h"

namespace
{
#pragma pack(push, 1)
struct AdmMode
{
    char ident[4];
    uint32_t unknown[5];
    uint32_t width;
    uint32_t height;
    uint32_t refreshMilliHz;
};

struct AdmWindow
{
    char ident[4];
    SDL_Window *window;
};
#pragma pack(pop)

AdmMode admMode = {};
AdmMode *admModeList[2] = {};
uint32_t admFbConfig = 0;
AdmWindow admWindow = {};

int returnSuccess()
{
    return 1;
}
const char *admGetString()
{
    return "Linux Loader Namco N2";
}

AdmMode **admChooseMode()
{
    std::memset(&admMode, 0, sizeof(admMode));
    std::memcpy(admMode.ident, "MOCF", 4);
    // Use the configured render size.
    admMode.width = static_cast<uint32_t>(getConfig()->width);
    admMode.height = static_cast<uint32_t>(getConfig()->height);
    admMode.refreshMilliHz = 60000;
    admModeList[0] = &admMode;
    admModeList[1] = nullptr;
    return admModeList;
}

uint32_t *admChooseFbConfig()
{
    return &admFbConfig;
}

static size_t patchWritableGlEntryPoints()
{
    if (!getSDLWindow())
        startSDL();

    const size_t patched =
        SymbolResolver::GetInstance().PatchNativeJumpStubs("gl", GLHooks_GetProcAddress);
    if (patched)
        log_info("Namco N2: initialized %zu writable OpenGL entry points", patched);
    return patched;
}

AdmWindow *admCreateWindow()
{
    patchWritableGlEntryPoints();
    std::memcpy(admWindow.ident, "WNDW", 4);
    admWindow.window = getSDLWindow();
    return admWindow.window ? &admWindow : nullptr;
}

int admMakeCurrent()
{
    SDL_Window *window = getSDLWindow();
    return window && getSDLContext() && SDL_GL_MakeCurrent(window, getSDLContext()) ? 1 : 0;
}

int admGetDeviceAttribi(int, int attribute, int *value)
{
    if (value)
        *value = 0;
    log_trace("Namco N2: display manager attribute 0x%X answered as zero", attribute);
    return 1;
}

int admSwapBuffers(AdmWindow *)
{
    const SDLFramePresentOptions present = {
        n2GetGameTitle(), true, nullptr, nullptr, nullptr, nullptr};
    return presentSDLFrame(&present);
}

//Counter-Strike Neo asks for the swap interval on every present
 
int admSwapInterval(int interval)
{
    static bool haveApplied = false;
    static bool announced = false;
    static int applied = 0;

    /* The loader owns the presentation rate while the limiter is on, so
     * [Graphics] VSYNC decides this rather than the guest. */
    if (getConfig()->fpsLimiter)
    {
        const int wanted = getConfig()->vsync ? 1 : 0;
        if (!announced && interval != wanted)
        {
            log_info("Namco N2: guest asked for swap interval %d; [Graphics] VSYNC gives %d",
                     interval, wanted);
            announced = true;
        }
        interval = wanted;
    }

    if (haveApplied && applied == interval)
        return 1;

    haveApplied = true;
    applied = interval;
    return setSDLSwapInterval(interval) ? 1 : 0;
}

using CreateTextureHandle = int (*)(void *, int, int);
using SetTexture = int (*)(void *, int, int);
using SetTextureRegion = int (*)(void *, int, int, int, int, int, int, void *);
using SetViewport = void (*)(void *, int, int, int, int, float, float);
using MakePerspectiveProjection = void (*)(void *, float, float, float, float, float);
using LuaGetGlobal = int (*)(void *, const char *);
using LuaSetGlobal = void (*)(void *, const char *);
using LuaPushNumber = void (*)(void *, double);

CreateTextureHandle originalCreateTextureHandle = nullptr;
SetTexture originalSetTexture = nullptr;
SetTextureRegion originalSetTextureRegion = nullptr;
SetViewport originalSetViewport = nullptr;
MakePerspectiveProjection originalMakePerspectiveProjection = nullptr;
LuaGetGlobal originalLuaGetGlobal = nullptr;
LuaSetGlobal luaSetGlobal = nullptr;
LuaPushNumber luaPushNumber = nullptr;
using FindMetaType = void *(*)(const char *);
FindMetaType originalFindMetaType = nullptr;
using ArenaMallocAligned = void *(*)(void *, uint32_t, uint32_t);
ArenaMallocAligned originalArenaMallocAligned = nullptr;
using InstantiateImage = void *(*)(void *);
InstantiateImage originalInstantiateImage = nullptr;
using AllocateImageMemory = void (*)(void *);
AllocateImageMemory originalAllocateImageMemory = nullptr;
using AutoSetImageParameters = void (*)(void *);
AutoSetImageParameters originalAutoSetImageParameters = nullptr;
thread_local bool registeringN2ShaderMetadata = false;
std::atomic_bool n2ShaderMetadataRegistered = false;
std::recursive_mutex n2ShaderMetadataMutex;

void registerN2ShaderMetadata()
{
    static const char *registrationSymbols[] = {
        "_ZN3Gap3Gfx19igGfxShaderConstant11arkRegisterEv",
        "_ZN3Gap3Gfx23igGfxShaderConstantList11arkRegisterEv",
        "_ZN3Gap3Gfx17igGfxShaderDefine11arkRegisterEv",
        "_ZN3Gap3Gfx21igGfxShaderDefineList11arkRegisterEv",
        "_ZN3Gap3Gfx22igTextureSamplerSource11arkRegisterEv",
        "_ZN3Gap3Gfx26igTextureSamplerSourceList11arkRegisterEv",
        "_ZN3Gap5Attrs17igPixelShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs21igPixelShaderAttrList11arkRegisterEv",
        "_ZN3Gap5Attrs18igVertexShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs22igVertexShaderAttrList11arkRegisterEv"
    };

    for (const char *symbol : registrationSymbols)
    {
        void (*registration)() = reinterpret_cast<void (*)()>(n2ResolveSymbol(symbol));
        if (registration)
            registration();
    }
}

void *findN2MetaType(const char *name)
{
    if (!name)
        return originalFindMetaType(name);
    const bool isShaderType =
        std::strstr(name, "igGfxShader") == name ||
        std::strstr(name, "igTextureSampler") == name ||
        std::strcmp(name, "igPixelShaderAttr") == 0 ||
        std::strcmp(name, "igPixelShaderAttrList") == 0 ||
        std::strcmp(name, "igVertexShaderAttr") == 0 ||
        std::strcmp(name, "igVertexShaderAttrList") == 0;

    if (isShaderType && !registeringN2ShaderMetadata &&
        !n2ShaderMetadataRegistered.load(std::memory_order_acquire))
    {
        std::lock_guard<std::recursive_mutex> lock(n2ShaderMetadataMutex);
        if (!n2ShaderMetadataRegistered.load(std::memory_order_relaxed))
        {
            registeringN2ShaderMetadata = true;
            registerN2ShaderMetadata();
            registeringN2ShaderMetadata = false;
            n2ShaderMetadataRegistered.store(true, std::memory_order_release);
            log_info("Namco N2: registered Alchemy shader metadata");
        }
    }
    return originalFindMetaType(name);
}

void *n2ArenaMallocAligned(void *pool, uint32_t size, uint32_t alignment)
{
    void *memory = originalArenaMallocAligned(pool, size, alignment);
    if (!memory)
    {
        using PoolSizeFn = uint32_t (*)(void *);
        static PoolSizeFn getTotalArenaSize = reinterpret_cast<PoolSizeFn>(
            n2ResolveSymbol("_ZNK3Gap4Core17igArenaMemoryPool17getTotalArenaSizeEv"));
        static PoolSizeFn getLargestAllocation = reinterpret_cast<PoolSizeFn>(
            n2ResolveSymbol("_ZNK3Gap4Core17igArenaMemoryPool33getLargestAvailableAllocationSizeEv"));
        const char *poolName = pool
            ? reinterpret_cast<const char *>(static_cast<uint8_t *>(pool) + 8)
            : "(null)";
        log_error("Namco N2: arena allocation failed pool=%s size=%u alignment=%u "
                  "total=%u largest=%u",
                  poolName, size, alignment,
                  pool && getTotalArenaSize ? getTotalArenaSize(pool) : 0,
                  pool && getLargestAllocation ? getLargestAllocation(pool) : 0);
    }
    return memory;
}

void *getN2FallbackMemoryPool()
{
    using PoolAdaptorFunction = void *(*)();
    using PoolAdaptorGet = void *(*)(void *);

    static PoolAdaptorFunction systemPoolFunction = reinterpret_cast<PoolAdaptorFunction>(
        n2ResolveSymbol("_ZN3Gap4Core27igMemoryPoolSystem_functionEv"));
    static PoolAdaptorGet getPool = reinterpret_cast<PoolAdaptorGet>(
        n2ResolveSymbol("_ZNK3Gap4Core19igMemoryPoolAdaptorptEv"));

    if (!systemPoolFunction || !getPool)
        return nullptr;

    void *adaptor = systemPoolFunction();
    return adaptor ? getPool(adaptor) : nullptr;
}

void *n2InstantiateImage(void *pool)
{
    void *image = originalInstantiateImage(pool);
    if (!image && !pool)
    {
        void *fallbackPool = getN2FallbackMemoryPool();
        if (fallbackPool)
            image = originalInstantiateImage(fallbackPool);
    }
    return image;
}

void n2AllocateImageMemory(void *image)
{
    if (!image)
        return;

    uint8_t *object = static_cast<uint8_t *>(image);
    void *&data = *reinterpret_cast<void **>(object + 0x34);
    uint32_t &imageSize = *reinterpret_cast<uint32_t *>(object + 0x30);
    const uint32_t compressedImageSize = GLHooks_ConsumeCompressedImageSize();

    if (compressedImageSize && compressedImageSize <= 256 * 1024 * 1024)
    {
        const uint32_t staleSize = imageSize;
        if (originalAutoSetImageParameters)
            originalAutoSetImageParameters(image);
        imageSize = compressedImageSize;
        if (!data)
        {
            void *fallbackPool = getN2FallbackMemoryPool();
            if (fallbackPool && originalArenaMallocAligned)
            {
                data = originalArenaMallocAligned(fallbackPool, imageSize, 128);
                if (data)
                {
                    object[0x3c] = 1;
                    if (staleSize != imageSize)
                        log_debug("Namco N2: corrected loadBuffer image size %u -> %u",
                                  staleSize, imageSize);
                    return;
                }
            }
        }

        originalAllocateImageMemory(image);
        imageSize = compressedImageSize;
        return;
    }

    originalAllocateImageMemory(image);
    if (!data && imageSize && imageSize <= 64 * 1024 * 1024)
    {
        void *fallbackPool = getN2FallbackMemoryPool();
        if (fallbackPool && originalArenaMallocAligned)
        {
            data = originalArenaMallocAligned(fallbackPool, imageSize, 128);
            if (data)
                object[0x3c] = 1;
        }
    }
}

bool isLoaderMainThread()
{
    return SDL_IsMainThread();
}

bool dispatchOnLoaderMainThread(SDL_MainThreadCallback callback, void *arguments)
{
    /* SDL owns the window and the context, so cross-thread work stays inside
     * that boundary rather than reaching into game objects.  admSwapBuffers()
     * services SDL's main-thread callback queue once per frame. */
    return runOnSDLMainThread(callback, arguments, false);
}

struct TextureHandleCall
{
    void *self;
    int width;
    int height;
};

void SDLCALL createTextureHandleOnMain(void *opaque)
{
    TextureHandleCall *call = static_cast<TextureHandleCall *>(opaque);
    originalCreateTextureHandle(call->self, call->width, call->height);
    delete call;
}

int createTextureHandle(void *self, int width, int height)
{
    if (isLoaderMainThread())
        return originalCreateTextureHandle(self, width, height);

    TextureHandleCall *call = new TextureHandleCall{
        self, width, height};
    if (dispatchOnLoaderMainThread(createTextureHandleOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal texture creation to the main thread");
    return originalCreateTextureHandle(self, width, height);
}

void setViewport(void *self, int x, int y, int width, int height,
                 float nearPlane, float farPlane)
{
    if (n2IsWanganTitle() && width == 88 && height == 82)
    {
        // Scale the minimap viewport.
        width = static_cast<int>(static_cast<float>(getConfig()->width) * 0.1375f);
        height = static_cast<int>(static_cast<float>(getConfig()->height) * 0.17f);
    }

    originalSetViewport(self, x, y, width, height, nearPlane, farPlane);
}

void makePerspectiveProjection(void *self, float fov, float a2,
                               float aspectRatio, float a4, float a5)
{
    constexpr float originalAspectRatio = 640.0f / 480.0f;
    const float configuredAspectRatio =
        static_cast<float>(getConfig()->width) / static_cast<float>(getConfig()->height);

    // Preserve the camera framing at the configured aspect.
    if (aspectRatio == originalAspectRatio)
        aspectRatio = configuredAspectRatio;
    fov = fov / originalAspectRatio * aspectRatio;

    originalMakePerspectiveProjection(self, fov, a2, aspectRatio, a4, a5);
}

int luaGetGlobal(void *state, const char *global)
{
    if (state && global && luaPushNumber && luaSetGlobal)
    {
        if (std::strcmp(global, "SCREEN_XSIZE") == 0)
        {
            luaPushNumber(state, static_cast<double>(getConfig()->width));
            luaSetGlobal(state, global);
        }
        else if (std::strcmp(global, "SCREEN_YSIZE") == 0)
        {
            luaPushNumber(state, static_cast<double>(getConfig()->height));
            luaSetGlobal(state, global);
        }
        else if (std::strcmp(global, "MINIMAP_DISP_X") == 0)
        {
            luaPushNumber(state, static_cast<double>(std::lround(
                static_cast<double>(getConfig()->width) * 0.0265625)));
            luaSetGlobal(state, global);
        }
        else if (std::strcmp(global, "MINIMAP_DISP_Y") == 0)
        {
            luaPushNumber(state, static_cast<double>(std::lround(
                static_cast<double>(getConfig()->height) * 0.2364)));
            luaSetGlobal(state, global);
        }
    }

    return originalLuaGetGlobal(state, global);
}

struct SetTextureCall
{
    void *self;
    int texture;
    int image;
};

void SDLCALL setTextureOnMain(void *opaque)
{
    SetTextureCall *call = static_cast<SetTextureCall *>(opaque);
    originalSetTexture(call->self, call->texture, call->image);
    delete call;
}

int setTexture(void *self, int texture, int image)
{
    if (isLoaderMainThread())
        return originalSetTexture(self, texture, image);

    SetTextureCall *call = new SetTextureCall{
        self, texture, image};
    if (dispatchOnLoaderMainThread(setTextureOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal texture upload to the main thread");
    return originalSetTexture(self, texture, image);
}

struct SetTextureRegionCall
{
    void *self;
    int texture;
    int x;
    int y;
    int width;
    int height;
    int format;
    void *image;
};

void SDLCALL setTextureRegionOnMain(void *opaque)
{
    SetTextureRegionCall *call = static_cast<SetTextureRegionCall *>(opaque);
    originalSetTextureRegion(call->self, call->texture, call->x, call->y,
                             call->width, call->height, call->format, call->image);
    delete call;
}

int setTextureRegion(void *self, int texture, int x, int y, int width, int height,
                     int format, void *image)
{
    if (isLoaderMainThread())
        return originalSetTextureRegion(self, texture, x, y, width, height, format, image);

    SetTextureRegionCall *call =
        new SetTextureRegionCall{self, texture, x, y, width, height, format, image};
    if (dispatchOnLoaderMainThread(setTextureRegionOnMain, call))
        return 1;

    delete call;
    log_warn("Namco N2: unable to marshal partial texture upload to the main thread");
    return originalSetTextureRegion(self, texture, x, y, width, height, format, image);
}
} // namespace

extern "C" int n2InstallAdmHooks(void)
{
    static bool installed = false;
    if (installed)
        return 1;
    if (!n2ResolveSymbol("admInitDevicei"))
        return 0;

    n2HookSymbol("admvt_setup", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admShutdown", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admGetString", reinterpret_cast<void *>(admGetString));
    n2HookSymbol("admGetNumDevices", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admInitDevicei", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admChooseModeConfigi", reinterpret_cast<void *>(admChooseMode));
    n2HookSymbol("admModeConfigi", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admChooseFBConfigi", reinterpret_cast<void *>(admChooseFbConfig));
    n2HookSymbol("admCreateScreeni", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admCreateGraphicsContext", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admCreateWindowi", reinterpret_cast<void *>(admCreateWindow));
    n2HookSymbol("admDisplayScreen", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admMakeContextCurrent", reinterpret_cast<void *>(admMakeCurrent));
    n2HookSymbol("admSwapInterval", reinterpret_cast<void *>(admSwapInterval));
    n2HookSymbol("admCursorAttribi", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("admGetDeviceAttribi", reinterpret_cast<void *>(admGetDeviceAttribi));
    n2HookSymbol("admSwapBuffers", reinterpret_cast<void *>(admSwapBuffers));
    n2HookSymbol("admSetMonitorGamma", reinterpret_cast<void *>(returnSuccess));

    if (!n2ArmHooks())
        return 0;

    if (getSDLWindow())
        patchWritableGlEntryPoints();

    installed = true;
    log_info("Namco N2: display manager entry points redirected to the loader's GL context");
    return 1;
}
extern "C" int n2InstallLateTextureHooks(void)
{
    static bool installed = false;
    if (installed)
        return 1;
    int hooks = 0;
    hooks += n2HookSymbolWithOriginal(
        "_ZN24clAlchemyTextureAccessor19createTextureHandleEii",
        reinterpret_cast<void *>(createTextureHandle),
        reinterpret_cast<void **>(&originalCreateTextureHandle));
    hooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext10setTextureEii",
        reinterpret_cast<void *>(setTexture),
        reinterpret_cast<void **>(&originalSetTexture));
    hooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext16setTextureRegionEiiiiiiPNS0_7igImageE",
        reinterpret_cast<void *>(setTextureRegion),
        reinterpret_cast<void **>(&originalSetTextureRegion));
    hooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext11setViewportEiiiiff",
        reinterpret_cast<void *>(setViewport),
        reinterpret_cast<void **>(&originalSetViewport));

    // Install resolution hooks before rendering.
    if (getConfig()->width != 640 || getConfig()->height != 480)
    {
        hooks += n2HookSymbolWithOriginal(
            "_ZN3Gap4Math11igMatrix44f32makePerspectiveProjectionRadiansEfffff",
            reinterpret_cast<void *>(makePerspectiveProjection),
            reinterpret_cast<void **>(&originalMakePerspectiveProjection));

        hooks += n2HookSymbolWithOriginal(
            "lua_getglobal",
            reinterpret_cast<void *>(luaGetGlobal),
            reinterpret_cast<void **>(&originalLuaGetGlobal));
        luaSetGlobal = reinterpret_cast<LuaSetGlobal>(n2ResolveSymbol("lua_setglobal"));
        luaPushNumber = reinterpret_cast<LuaPushNumber>(n2ResolveSymbol("lua_pushnumber"));
    }

    if (!n2ArmHooks())
        return 0;

    installed = true;
    log_info("Namco N2: late render hooks installed (%d hooks, detected=%d)",
             hooks, n2IsDetected());
    return hooks > 0 ? 1 : 0;
}

extern "C" int n2InstallTextureDispatchHooks(void)
{
    int hooks = 0;
    hooks += n2HookSymbolWithOriginal(
        "_ZN24clAlchemyTextureAccessor19createTextureHandleEii",
        reinterpret_cast<void *>(createTextureHandle),
        reinterpret_cast<void **>(&originalCreateTextureHandle));
    hooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext10setTextureEii",
        reinterpret_cast<void *>(setTexture),
        reinterpret_cast<void **>(&originalSetTexture));
    hooks += n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx19igAGLEVisualContext16setTextureRegionEiiiiiiPNS0_7igImageE",
        reinterpret_cast<void *>(setTextureRegion),
        reinterpret_cast<void **>(&originalSetTextureRegion));
    return hooks;
}

extern "C" void n2InstallAlchemyImageHooks(void)
{
    n2HookSymbolWithOriginal(
        "_ZN3Gap4Core12igMetaObject8findTypeEPKc",
        reinterpret_cast<void *>(findN2MetaType),
        reinterpret_cast<void **>(&originalFindMetaType));
    n2HookSymbolWithOriginal(
        "_ZN3Gap4Core17igArenaMemoryPool13mallocAlignedEjj",
        reinterpret_cast<void *>(n2ArenaMallocAligned),
        reinterpret_cast<void **>(&originalArenaMallocAligned));
    n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx7igImage20_instantiateFromPoolEPNS_4Core12igMemoryPoolE",
        reinterpret_cast<void *>(n2InstantiateImage),
        reinterpret_cast<void **>(&originalInstantiateImage));
    originalAutoSetImageParameters = reinterpret_cast<AutoSetImageParameters>(
        n2ResolveSymbol("_ZN3Gap3Gfx7igImage25autoSetUnfilledParametersEv"));
    n2HookSymbolWithOriginal(
        "_ZN3Gap3Gfx7igImage19allocateImageMemoryEv",
        reinterpret_cast<void *>(n2AllocateImageMemory),
        reinterpret_cast<void **>(&originalAllocateImageMemory));
}

extern "C" int n2InitializeGraphics(void)
{
    if (!n2IsDetected())
        return 0;

    /* A title whose engine drives the display manager itself needs the loader
     * to bring the window up first when the GL stubs were not patchable. */
    if (patchWritableGlEntryPoints() == 0 && n2TitleQuirks()->needsLoaderOwnedWindow)
        return 1;
    return 0;
}

#endif
