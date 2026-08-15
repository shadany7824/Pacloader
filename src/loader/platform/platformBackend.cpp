#include "platformBackend.h"

#include "../config/config.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/es1Title.h"
#include "../hardware/namco/n2/n2Title.h"
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
#if defined(_WIN32) || defined(__MINGW32__)
HWND g_lockedWindow = nullptr;
LONG g_lockedWindowX = 0;
LONG g_lockedWindowY = 0;
bool g_lockedWindowPosition = false;
bool g_userMovingWindow = false;
WNDPROC g_previousWindowProc = nullptr;

LRESULT CALLBACK lockedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ENTERSIZEMOVE)
        g_userMovingWindow = true;

    if (message == WM_WINDOWPOSCHANGING && g_lockedWindowPosition &&
        window == g_lockedWindow && !g_userMovingWindow)
    {
        WINDOWPOS *position = reinterpret_cast<WINDOWPOS *>(lParam);
        if (position && !(position->flags & SWP_NOMOVE))
        {
            position->x = g_lockedWindowX;
            position->y = g_lockedWindowY;
        }
    }

    if (message == WM_EXITSIZEMOVE)
    {
        g_userMovingWindow = false;
        RECT rect{};
        if (window == g_lockedWindow && GetWindowRect(window, &rect))
        {
            g_lockedWindowX = rect.left;
            g_lockedWindowY = rect.top;
        }
    }

    if (g_previousWindowProc)
        return CallWindowProcW(g_previousWindowProc, window, message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}
#endif
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

extern "C" int platformWindowIsFixedSize(void)
{
    return platformIsES1() ? es1TitleQuirks()->fixedWindowSize : 0;
}

extern "C" int platformHasHPatternShifter(void)
{
    return platformIsES1() ? es1TitleQuirks()->hasHPatternShifter : 0;
}

extern "C" int platformCardInsertIsCommand(void)
{
    if (platformIsES1())
        return es1TitleQuirks()->cardInsertIsCommand;
    if (platformIsN2())
        return n2TitleQuirks()->cardInsertIsCommand;
    return 0;
}

extern "C" int platformTestSwitchIsLatching(void)
{
    return platformIsES1() ? es1TitleQuirks()->testSwitchIsLatching : 0;
}

extern "C" int platformPollsSteeringAxisEachFrame(void)
{
    return platformIsES1() ? es1TitleQuirks()->pollsSteeringAxisEachFrame : 0;
}

extern "C" int platformReportsWindowedScreen(void)
{
    return platformIsES1() ? es1TitleQuirks()->reportsWindowedScreen : 0;
}

extern "C" int platformJvsPedalMaximum(void)
{
    return platformIsES1() ? es1TitleQuirks()->jvsPedalMaximum : 0;
}

extern "C" const char *platformControlsProfileName(void)
{
    if (platformIsES1())
    {
        const Es1Title *title = es1CurrentTitle();
        return title ? title->controlsProfileName : NULL;
    }
    if (platformIsN2())
    {
        const N2Title *title = n2CurrentTitle();
        return title ? title->controlsProfileName : NULL;
    }
    return NULL;
}

extern "C" int platformCabinetPanel(void)
{
    if (platformIsES1())
        return es1TitleQuirks()->cabinetPanel;
    if (platformIsN2())
        return n2TitleQuirks()->cabinetPanel;
    return CABINET_PANEL_GENERIC;
}

extern "C" int platformRaiseNativeWindow(void *nativeWindow)
{
#if defined(_WIN32) || defined(__MINGW32__)
    HWND window = static_cast<HWND>(nativeWindow);
    if (!window || !IsWindow(window))
        return 0;
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

extern "C" void platformRememberWindowPosition(void *nativeWindow)
{
#if defined(_WIN32) || defined(__MINGW32__)
    HWND window = static_cast<HWND>(nativeWindow);
    RECT rect{};
    if (!window || !IsWindow(window) || !GetWindowRect(window, &rect))
        return;

    g_lockedWindow = window;
    g_lockedWindowX = rect.left;
    g_lockedWindowY = rect.top;
    g_lockedWindowPosition = true;

    if (!g_previousWindowProc)
    {
        g_previousWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(lockedWindowProc)));
    }
#else
    (void)nativeWindow;
#endif
}
