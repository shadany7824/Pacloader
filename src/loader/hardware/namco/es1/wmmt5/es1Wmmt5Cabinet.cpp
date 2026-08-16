#include "es1Wmmt5Cabinet.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>

#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* Two of the unit-info check's steps ask a terminal cabinet about itself and
 * never finish without one, so a lone drive cabinet skips past them. */
namespace
{

constexpr uintptr_t UpdateCheckAddress = 0x084fa120;
constexpr uint8_t UpdateCheckSignature[] = {0x55, 0x89, 0xe5, 0x81, 0xec, 0x08,
                                            0x05, 0x00, 0x00, 0x89, 0x5d, 0xf4};

/* Offsets into the check's own state object. */
constexpr size_t StepOffset = 568;
constexpr size_t TerminalAnsweredOffset = 1096;
constexpr int StepAskTerminal = 9;
constexpr int StepWaitTerminal = 20;
constexpr int StepAfterTerminal = 22;

void (*g_originalUpdateCheck)(uint8_t *, int) = nullptr;

void wmmt5UpdateUnitCheck(uint8_t *state, int argument)
{
    GuestTls::HostCallScope hostCall;
    if (state)
    {
        int &step = *reinterpret_cast<int *>(state + StepOffset);
        if (step == StepAskTerminal)
        {
            step = StepAfterTerminal;
            *reinterpret_cast<int *>(state + TerminalAnsweredOffset) = 1;
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
    if (getConfig()->namcoES1.cabinetMode != NAMCO_ES1_CABINET_DRIVE)
        return;

    const Es1HookSpec hooks[] = {
        {UpdateCheckAddress, reinterpret_cast<void *>(wmmt5UpdateUnitCheck), "unitInfoCheck",
         reinterpret_cast<void **>(&g_originalUpdateCheck), UpdateCheckSignature,
         sizeof(UpdateCheckSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 cabinet");
}

#endif
