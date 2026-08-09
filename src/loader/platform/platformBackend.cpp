#include "platformBackend.h"

#include "../config/config.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/n2/n2.h"
#include "../hardware/namco/n2/n2CardReader.h"
#include "../hardware/namco/es1/es1VirtualDevices.h"
#include "../hardware/namco/n2/n2VirtualDevices.h"
#include "../log/log.h"

#if defined(_WIN32) || defined(__MINGW32__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
bool g_detected = false;
}

extern "C" int platformPrepareLoad(const char *elfPath)
{
    /* ES1 is checked first by design; N2 and ES1 are separate boards. */
    if (es1DetectGame(elfPath))
    {
        es1PrepareLoad(elfPath);
        return 0;
    }
    n2PrepareLoad(elfPath);
    return 0;
}

extern "C" int platformDetectGame(const char *elfPath)
{
    g_detected = false;

    if (es1DetectGame(elfPath))
    {
        g_detected = true;
        return 1;
    }
    if (n2DetectGame(elfPath))
    {
        g_detected = true;
        return 1;
    }
    return 0;
}

extern "C" int platformIsDetected(void)
{
    return g_detected ? 1 : 0;
}

extern "C" int platformIsN2(void)
{
    return g_detected && n2IsDetected();
}

extern "C" int platformIsES1(void)
{
    return g_detected && es1IsDetected();
}

extern "C" int platformInstallHooks(void)
{
    if (platformIsES1())
        return es1InstallHooks();
    if (platformIsN2())
        return n2InstallHooks();
    return -1;
}

extern "C" int platformInstallAdmHooks(void)
{
    if (platformIsN2())
        return n2InstallAdmHooks();
    return 0;
}

extern "C" int platformInstallLateHooks(void)
{
    if (platformIsES1())
        return es1InstallLateHooks();
    if (platformIsN2())
        return n2InstallLateTextureHooks();
    return -1;
}

extern "C" int platformInitializeGraphics(void)
{
    if (platformIsES1())
        return es1InitializeGraphics();
    if (platformIsN2())
        return n2InitializeGraphics();
    return -1;
}

extern "C" int platformHandleSystemCommand(const char *command)
{
    if (platformIsES1())
        return es1HandleSystemCommand(command);
    if (platformIsN2())
        return n2HandleSystemCommand(command);
    return -1;
}

extern "C" int platformHandleHostKey(int key, uint32_t modifiers)
{
    return platformHandleHostKeyEvent(key, modifiers, 1);
}

extern "C" int platformHandleHostKeyEvent(int key, uint32_t modifiers, int pressed)
{
    if (platformIsN2() && pressed)
        return n2HandleHostKey(key, modifiers);
    return 0;
}

extern "C" int platformWantsCabinetArgument(void)
{
    return platformIsN2() ? 1 : 0;
}

extern "C" const char *platformName(void)
{
    if (platformIsES1())
        return "Namco System ES1";
    if (platformIsN2())
        return "Namco System N2";
    return "Unknown";
}

extern "C" void platformRegisterVirtualDevices(void)
{
    /* Registration is done before detection; each provider gates its claims. */
    n2RegisterVirtualDevices();
    es1RegisterVirtualDevices();
}

extern "C" void platformRegisterCardControl(void)
{
    if (platformIsN2())
        n2CardReaderRegisterCardControl();
}

extern "C" int platformRaiseNativeWindow(void *nativeWindow)
{
#if defined(_WIN32) || defined(__MINGW32__)
    HWND window = static_cast<HWND>(nativeWindow);
    if (!window || !IsWindow(window))
        return 0;
    const bool keepTopmost = getConfig()->alwaysOnTop != 0;

    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    else
        ShowWindow(window, SW_SHOW);

    /* Windows normally rejects SetForegroundWindow when a process was
     * launched behind another foreground process. Temporarily joining the
     * relevant input queues makes this equivalent to a user-requested window
     * activation, which is what the guest's XRaiseWindow call represents. */
    HWND foreground = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD windowThread = GetWindowThreadProcessId(window, nullptr);
    const DWORD foregroundThread = foreground
                                       ? GetWindowThreadProcessId(foreground, nullptr)
                                       : 0;

    const bool attachedWindow = windowThread && windowThread != currentThread &&
                                AttachThreadInput(currentThread, windowThread, TRUE);
    const bool attachedForeground = foregroundThread && foregroundThread != currentThread &&
                                    foregroundThread != windowThread &&
                                    AttachThreadInput(currentThread, foregroundThread, TRUE);

    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);

    /* A topmost round-trip is a visual fallback for foreground-lock policies.
     * HWND_NOTOPMOST immediately restores the normal Z-order class, so this
     * does not turn the game into a permanent always-on-top window. */
    if (GetForegroundWindow() != window)
    {
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (!keepTopmost)
            SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(window);
    }

    if (keepTopmost)
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    if (attachedForeground)
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    if (attachedWindow)
        AttachThreadInput(currentThread, windowThread, FALSE);

    return GetForegroundWindow() == window ? 1 : 0;
#else
    (void)nativeWindow;
    return 0;
#endif
}
