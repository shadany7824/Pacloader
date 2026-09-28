#include "es1Wmmt5Cabinet.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>

#include "es1Wmmt5Build.hpp"
#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* Two of the unit-info check's steps ask a terminal cabinet about itself and
 * never finish without one, so a lone drive cabinet skips past them. */
namespace
{

/* The frame size differs between builds, so only the frame setup is checked. */
constexpr uint8_t UpdateCheckSignature[] = {0x55, 0x89, 0xe5, 0x81, 0xec};

/* The check's steps; the same numbering in every build. */
constexpr int StepAskTerminal = 9;
constexpr int StepWaitTerminal = 20;
constexpr int StepAfterTerminal = 22;

size_t g_stepOffset = 0;
size_t g_terminalAnsweredOffset = 0;

void (*g_originalUpdateCheck)(uint8_t *, int) = nullptr;

/* The check's last step asks the terminal link whether the terminal has sent
 * its serial; without a terminal it stops on E2407 and the boot screen stays
 * at TERMINAL UNIT S/N CHECKING. The flag's offset differs between builds. */
constexpr uint8_t TerminalSerialKnownSignature[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08,
                                                    0x5d, 0x8b, 0x00, 0x0f, 0xb6, 0x80};

bool wmmt5TerminalSerialKnown(void *) { return true; }

void wmmt5UpdateUnitCheck(uint8_t *state, int argument)
{
    GuestTls::HostCallScope hostCall;
    if (state)
    {
        int &step = *reinterpret_cast<int *>(state + g_stepOffset);
        if (step == StepAskTerminal)
        {
            step = StepAfterTerminal;
            *reinterpret_cast<int *>(state + g_terminalAnsweredOffset) = 1;
        }
        else if (step == StepWaitTerminal)
        {
            step = StepAfterTerminal;
        }
    }

    if (g_originalUpdateCheck)
    {
        GuestTls::EnterGuestCode();
        g_originalUpdateCheck(state, argument);
        GuestTls::EnterHostCall();
    }
}

} // namespace

void es1Wmmt5InstallCabinetHooks(void)
{
    const Wmmt5Build *build = es1Wmmt5Build();
    if (!build || getConfig()->namcoES1.cabinetMode != NAMCO_ES1_CABINET_DRIVE)
        return;

    g_stepOffset = build->unitStepOffset;
    g_terminalAnsweredOffset = build->unitTerminalAnsweredOffset;

    const Es1HookSpec hooks[] = {
        {build->unitCheck, reinterpret_cast<void *>(wmmt5UpdateUnitCheck), "unitInfoCheck",
         reinterpret_cast<void **>(&g_originalUpdateCheck), UpdateCheckSignature,
         sizeof(UpdateCheckSignature)},
        {build->terminalSerialKnown, reinterpret_cast<void *>(wmmt5TerminalSerialKnown),
         "terminalSerialKnown", nullptr, TerminalSerialKnownSignature,
         sizeof(TerminalSerialKnownSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 cabinet");
}

#endif
