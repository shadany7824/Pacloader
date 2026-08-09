#if defined(_WIN32) || defined(__MINGW32__)

#include "glxBridge.hpp"

#include "../config/config.h"
#include "../graphics/fpsLimiter.h"
#include "../graphics/sdlCalls.h"
#include "../log/log.h"
#include "symbolResolver.hpp"
#include "glHooks.hpp"

#include <SDL3/SDL.h>
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <vector>

extern "C" void *bridgeX11CurrentDisplay();

namespace
{
struct VisualInfo
{
    void *visual;
    unsigned long visualid;
    int screen;
    int depth;
    int classType;
    unsigned long redMask;
    unsigned long greenMask;
    unsigned long blueMask;
    int colormapSize;
    int bitsPerRgb;
};

VisualInfo g_visual{reinterpret_cast<void *>(1), 1, 0, 24, 4,
                    0x00ff0000, 0x0000ff00, 0x000000ff, 256, 8};

/* Every glXCreateContext gets its own context sharing the primary's objects: one
 * shared context fails wglMakeCurrent with ERROR_BUSY on the worker threads
 * titles upload from.  They come from SDL and are built at initialisation. */
struct ContextPool
{
    SRWLOCK lock;
    SDL_GLContext primary;
    bool prepared;
    std::vector<SDL_GLContext> free;   /* Built up front, handed out on demand. */
    std::vector<SDL_GLContext> owned;  /* Everything we created, for cleanup. */
};

ContextPool g_contexts{SRWLOCK_INIT, nullptr, false, {}, {}};

/* Covers the ES1 resource workers seen so far, with room to spare. */
constexpr size_t kSharedContextPool = 8;

bool isPrimaryContext(void *context)
{
    return context != nullptr && context == static_cast<void *>(g_contexts.primary);
}

/* Runs once, on the thread that just created the primary context and still
 * holds it current. */
void prepareSharedContexts()
{
    if (g_contexts.prepared)
        return;
    g_contexts.prepared = true;

    SDL_Window *window = getSDLWindow();
    if (!window || !g_contexts.primary)
        return;

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    for (size_t i = 0; i < kSharedContextPool; ++i)
    {
        /* Each new context becomes current, so rebind the primary first and
         * every member shares directly with it. */
        if (!makeSDLCurrent(window, g_contexts.primary))
            break;

        SDL_GLContext created = SDL_GL_CreateContext(window);
        if (!created)
        {
            log_warn("ES1 GLX: SDL_GL_CreateContext failed after %zu shared contexts (%s)",
                     i, SDL_GetError());
            break;
        }

        g_contexts.free.push_back(created);
        g_contexts.owned.push_back(created);
    }

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    makeSDLCurrent(window, g_contexts.primary);

    log_info("ES1 GLX: prepared %zu GL contexts sharing with the primary",
             g_contexts.free.size());
}

extern "C" void *bridgeGlxChooseVisual(void *display, int screen, int *attributes)
{
    (void)display;
    (void)screen;
    (void)attributes;
    log_debug("ES1 GLX: glXChooseVisual");
    return &g_visual;
}

extern "C" void **bridgeGlxChooseFBConfig(void *display, int screen, int *attributes,
                                           int *count)
{
    (void)display;
    (void)screen;
    (void)attributes;
    static void *configs[] = {&g_visual};
    if (count)
        *count = 1;
    return configs;
}

extern "C" void *bridgeGlxCreateContext(void *display, void *visual, void *share,
                                         int direct)
{
    (void)display;
    (void)visual;
    (void)share;
    (void)direct;
    if (!getSDLWindow())
        startSDL();

    const unsigned long tid = static_cast<unsigned long>(GetCurrentThreadId());

    AcquireSRWLockExclusive(&g_contexts.lock);
    if (!g_contexts.primary)
    {
        g_contexts.primary = getSDLContext();
        prepareSharedContexts();
        ReleaseSRWLockExclusive(&g_contexts.lock);

        log_debug("ES1 GLX: glXCreateContext tid=%lu -> primary %p", tid,
                  static_cast<void *>(g_contexts.primary));
        return getSDLContext();
    }

    if (g_contexts.free.empty())
    {
        ReleaseSRWLockExclusive(&g_contexts.lock);
        /* Sharing one context still beats handing back something the guest
         * cannot bind, so keep the old behaviour as the fallback. */
        log_warn("ES1 GLX: shared context pool exhausted; reusing the primary context");
        return getSDLContext();
    }

    SDL_GLContext handed = g_contexts.free.back();
    g_contexts.free.pop_back();
    ReleaseSRWLockExclusive(&g_contexts.lock);

    log_debug("ES1 GLX: glXCreateContext tid=%lu -> shared %p", tid, static_cast<void *>(handed));
    return handed;
}

extern "C" void bridgeGlxDestroyContext(void *display, void *context)
{
    (void)display;
    /* SDL owns the primary context; the guest must not destroy it.  The extra
     * worker contexts are ours, so those we do release. */
    if (!context || isPrimaryContext(context))
        return;

    SDL_GLContext released = static_cast<SDL_GLContext>(context);

    AcquireSRWLockExclusive(&g_contexts.lock);
    const bool ours = std::find(g_contexts.owned.begin(), g_contexts.owned.end(), released) !=
                      g_contexts.owned.end();
    const bool alreadyFree = std::find(g_contexts.free.begin(), g_contexts.free.end(), released) !=
                             g_contexts.free.end();
    if (ours && !alreadyFree)
    {
        /* Recycle rather than destroy: the sharing group can only be built
         * during initialisation, so a destroyed context cannot be replaced. */
        g_contexts.free.push_back(released);
    }
    ReleaseSRWLockExclusive(&g_contexts.lock);

    if (ours && SDL_GL_GetCurrentContext() == released)
        makeSDLCurrent(nullptr, nullptr);
}

extern "C" int bridgeGlxMakeCurrent(void *display, unsigned long drawable,
                                     void *context)
{
    (void)display;
    bool success = false;

    if (drawable == 0 || context == nullptr)
        success = makeSDLCurrent(nullptr, nullptr);
    else
        success = makeSDLCurrent(getSDLWindow(), static_cast<SDL_GLContext>(context));

    const char *failure = success ? "ok" : SDL_GetError();

    log_debug("ES1 GLX: glXMakeCurrent tid=%lu drawable=%lu context=%p -> %d (%s)",
              static_cast<unsigned long>(GetCurrentThreadId()), drawable, context,
              success ? 1 : 0, success ? "ok" : failure);
    return success ? 1 : 0;
}

extern "C" void *bridgeGlxCreateNewContext(void *display, void *visual, int renderType,
                                            void *share, int direct)
{
    return bridgeGlxCreateContext(display, visual, share, direct);
}

extern "C" void bridgeGlxCopyContext(void *display, void *source, void *destination,
                                      unsigned long mask)
{
    (void)display;
    (void)source;
    (void)destination;
    (void)mask;
}

extern "C" void *bridgeGlxGetCurrentContext()
{
    /* Per-thread, like GLX: a worker must not be told it holds the render
     * thread's context. */
    if (SDL_GLContext current = SDL_GL_GetCurrentContext())
        return current;
    return getSDLContext();
}

extern "C" void *bridgeGlxGetCurrentDisplay()
{
    return bridgeX11CurrentDisplay();
}

extern "C" unsigned long bridgeGlxGetCurrentDrawable()
{
    return getSDLWindow() ? 1UL : 0UL;
}

extern "C" unsigned long bridgeGlxGetCurrentReadDrawable()
{
    return bridgeGlxGetCurrentDrawable();
}

extern "C" int bridgeGlxMakeContextCurrent(void *display, unsigned long draw,
                                            unsigned long read, void *context)
{
    (void)read;
    return bridgeGlxMakeCurrent(display, draw, context);
}

extern "C" int bridgeGlxQueryContext(void *display, void *context, int attribute, int *value)
{
    (void)display;
    (void)context;
    (void)attribute;
    if (value)
        *value = 0;
    return 0;
}

extern "C" int bridgeGlxWaitGL()
{
    return 0;
}

extern "C" void bridgeGlxSwapBuffers(void *display, unsigned long drawable)
{
    (void)display;
    log_debug("ES1 GLX: glXSwapBuffers drawable=%lu", drawable);
    if (getSDLWindow())
    {
        pollEvents();
        if (getConfig()->fpsLimiter)
            frameTiming();
        SDL_GL_SwapWindow(getSDLWindow());
    }
}

extern "C" int bridgeGlxSwapIntervalSGI(int interval)
{
    /* System ES1 requests this legacy GLX entry point during X-system
     * initialization. When the loader limiter is enabled, disable vsync so
     * [Graphics] FPS_TARGET remains the single presentation-rate control. */
    if (getConfig()->fpsLimiter && interval != 0)
        interval = 0;

    return setSDLSwapInterval(interval) ? 0 : 1;
}

extern "C" int bridgeGlxGetVideoSyncSGI(unsigned int *count)
{
    static unsigned int videoSyncCount = 0;
    if (count)
        *count = ++videoSyncCount;
    return 0;
}

extern "C" int bridgeGlxWaitVideoSyncSGI(int divisor, int remainder,
                                           unsigned int *count)
{
    (void)divisor;
    (void)remainder;
    return bridgeGlxGetVideoSyncSGI(count);
}

extern "C" void *bridgeGlxGetProcAddress(const char *name)
{
    if (!name)
        return nullptr;
    if (std::strcmp(name, "glXSwapIntervalSGI") == 0)
        return reinterpret_cast<void *>(bridgeGlxSwapIntervalSGI);
    if (std::strcmp(name, "glXGetVideoSyncSGI") == 0)
        return reinterpret_cast<void *>(bridgeGlxGetVideoSyncSGI);
    if (std::strcmp(name, "glXWaitVideoSyncSGI") == 0)
        return reinterpret_cast<void *>(bridgeGlxWaitVideoSyncSGI);
    return GLHooks_GetProcAddress(name);
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace GlxBridge
{
void initBridges()
{
    map("glXChooseFBConfig", bridgeGlxChooseFBConfig);
    map("glXChooseVisual", bridgeGlxChooseVisual);
    map("glXCopyContext", bridgeGlxCopyContext);
    map("glXCreateContext", bridgeGlxCreateContext);
    map("glXCreateNewContext", bridgeGlxCreateNewContext);
    map("glXDestroyContext", bridgeGlxDestroyContext);
    map("glXGetCurrentContext", bridgeGlxGetCurrentContext);
    map("glXGetCurrentDisplay", bridgeGlxGetCurrentDisplay);
    map("glXGetCurrentDrawable", bridgeGlxGetCurrentDrawable);
    map("glXGetCurrentReadDrawable", bridgeGlxGetCurrentReadDrawable);
    map("glXMakeContextCurrent", bridgeGlxMakeContextCurrent);
    map("glXMakeCurrent", bridgeGlxMakeCurrent);
    map("glXQueryContext", bridgeGlxQueryContext);
    map("glXSwapBuffers", bridgeGlxSwapBuffers);
    map("glXSwapIntervalSGI", bridgeGlxSwapIntervalSGI);
    map("glXGetVideoSyncSGI", bridgeGlxGetVideoSyncSGI);
    map("glXWaitVideoSyncSGI", bridgeGlxWaitVideoSyncSGI);
    map("glXGetProcAddress", bridgeGlxGetProcAddress);
    map("glXGetProcAddressARB", bridgeGlxGetProcAddress);
    map("glXWaitGL", bridgeGlxWaitGL);
    log_info("Initialized GLX compatibility bridges");
}
}

#endif
