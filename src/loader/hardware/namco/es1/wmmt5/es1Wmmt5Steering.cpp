#include "es1Wmmt5Steering.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>

#include "../es1CompatLayer.h"
#include "../es1Kickback.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../log/log.h"

/* The STR3 steering PCB, stepped from the title's own power transitions.
 * TODO: FFBIo::State_InitialWait never leaves; the reply it wants is unknown. */
namespace
{

constexpr uintptr_t PowerOnAddress = 0x080dfd50;
constexpr uintptr_t PowerOffAddress = 0x080dfcc0;

/* The guard is against another build, not against each other. */
constexpr uint8_t PowerSignature[] = {0x55, 0x89, 0xe5, 0x56, 0x53, 0x83, 0xec, 0x20};

int (*g_originalPowerOn)(int) = nullptr;
int (*g_originalPowerOff)(int) = nullptr;

int wmmt5SteeringPowerOn(int object)
{
    GuestTls::HostCallScope hostCall;
    es1KickbackReportPoweredSelfCheck();
    return g_originalPowerOn ? g_originalPowerOn(object) : 0;
}

int wmmt5SteeringPowerOff(int object)
{
    GuestTls::HostCallScope hostCall;
    es1KickbackReportMotorPower(0);
    return g_originalPowerOff ? g_originalPowerOff(object) : 0;
}

} // namespace

void es1Wmmt5InstallSteeringHooks(void)
{
    const Es1HookSpec hooks[] = {
        {PowerOnAddress, reinterpret_cast<void *>(wmmt5SteeringPowerOn), "FFBIo_State_PowerOn",
         reinterpret_cast<void **>(&g_originalPowerOn), PowerSignature, sizeof(PowerSignature)},
        {PowerOffAddress, reinterpret_cast<void *>(wmmt5SteeringPowerOff), "FFBIo_State_PowerOff",
         reinterpret_cast<void **>(&g_originalPowerOff), PowerSignature, sizeof(PowerSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 steering");
}

#endif
