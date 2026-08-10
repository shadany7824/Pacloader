#include "es1Wmmt4Ffb.hpp"
#include "../es1Kickback.h"
#include "../es1CompatLayer.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../ffb/sdlFfbBackend.h"
#include "../../../../config/config.h"
#include "../../../../log/log.h"

#include <cstdint>
#include <cstring>

namespace
{
/* WMMT4's FFB setters and update entry points. */
constexpr uintptr_t SetTorqueAddress = 0x8367ae0;   /* float, -1..1 */
constexpr uintptr_t SetPeriodAddress = 0x8367b10;   /* int */
constexpr uintptr_t SetSpringAddress = 0x8367b30;   /* float, 0..1 */
constexpr uintptr_t SetDamperAddress = 0x8367b60;   /* float, 0..1 */
constexpr uintptr_t SetVibrateAddress = 0x8367b90;  /* float, 0..1 */
/* Per-frame dispatch: force, spring, damper and vibration floats. */
constexpr uintptr_t ApplyFfbAddress = 0x8366e30;
/* Wall-hit effects are staged here before the update loop dispatches them. */
constexpr uintptr_t SetOneShotEffectAddress = 0x8367ce0;
constexpr uintptr_t PowerOnAddress = 0x8366f80;
constexpr uintptr_t PowerOffAddress = 0x8366fb0;
constexpr uintptr_t StartSelfCheckAddress = 0x8367ee0;
constexpr uintptr_t CompleteDriverSelfCheckAddress = 0x8366230;

/* Field offsets in that object, in the order the setters write them. */
constexpr size_t TorqueOffset = 0x28;
constexpr size_t PeriodOffset = 0x2c;
constexpr size_t SpringOffset = 0x30;
constexpr size_t DamperOffset = 0x34;
constexpr size_t VibrateOffset = 0x38;

using SetFloat = void (*)(void *, float);
using SetInt = void (*)(void *, int);
using ApplyFfb = void (*)(void *, float, float, float, float);
using SetPower = void (*)(void *, int);
using StartSelfCheck = void (*)(void *);
using CompleteDriverSelfCheck = int (*)(void *);

SetFloat g_originalTorque = nullptr;
SetInt g_originalPeriod = nullptr;
SetFloat g_originalSpring = nullptr;
SetFloat g_originalDamper = nullptr;
SetFloat g_originalVibrate = nullptr;
ApplyFfb g_originalApplyFfb = nullptr;
ApplyFfb g_originalSetOneShotEffect = nullptr;
SetPower g_originalPowerOn = nullptr;
SetPower g_originalPowerOff = nullptr;
StartSelfCheck g_originalStartSelfCheck = nullptr;

void completeVirtualSelfCheck(void *object)
{
    if (!object)
        return;

    void *driver = nullptr;
    std::memcpy(&driver, static_cast<const uint8_t *>(object) + 4, sizeof(driver));
    if (!driver)
        return;

    /* Complete the title-side self-check and clear its pending flag. */
    GuestTls::EnterGuestCode();
    reinterpret_cast<CompleteDriverSelfCheck>(CompleteDriverSelfCheckAddress)(driver);
    GuestTls::EnterHostCall();
    static_cast<uint8_t *>(object)[0x50] = 0;
}

constexpr float NeutralForceThreshold = 0.08f;
constexpr unsigned int NeutralFramesRequired = 3;

unsigned long g_sequence = 0;
bool g_powerActive = false;
bool g_running = false;
bool g_outputArmed = false;
bool g_oneShotPending = false;
unsigned int g_neutralFrames = 0;

float scalePercent(int value)
{
    if (value < 0)
        value = 0;
    if (value > 200)
        value = 200;
    return static_cast<float>(value) / 100.0f;
}

float clampSigned(float value)
{
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

float absoluteValue(float value)
{
    return value < 0.0f ? -value : value;
}

void disableHostFfb()
{
    FfbSteeringState disabled = {};
    disabled.enabled = 0;
    sdlFfbApplySteering(&disabled);
}

float readFloat(const void *object, size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}

int readInt(const void *object, size_t offset)
{
    int value = 0;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}

/* Read the complete FFB object after individual setter calls. */
void publish(const void *object)
{
    const EmulatorConfig *config = getConfig();
    if (!object || !config->namcoES1.forceFeedbackEnabled)
        return;

    /* Suppress stale object values during menus and self-test. */
    if (!g_powerActive || !g_running)
    {
        disableHostFfb();
        return;
    }

    /* Accept cached torque only after the live output hook arms the state. */
    if (!g_outputArmed)
    {
        disableHostFfb();
        return;
    }

    float torque = readFloat(object, TorqueOffset) * scalePercent(config->namcoES1.ffbGain) *
                   scalePercent(config->namcoES1.ffbWeight);
    if (config->namcoES1.ffbInvert)
        torque = -torque;
    const float spring = readFloat(object, SpringOffset) *
                         scalePercent(config->namcoES1.ffbSpringGain);
    const float damper = readFloat(object, DamperOffset) *
                         scalePercent(config->namcoES1.ffbDamperGain);
    const float vibration = readFloat(object, VibrateOffset) *
                            scalePercent(config->namcoES1.ffbVibrationGain);
    const int period = readInt(object, PeriodOffset);

    FfbSteeringState state = {};
    state.enabled = 1;
    state.constantForce = torque;
    state.springStrength = spring;
    state.damperStrength = damper;
    state.damperFloor = scalePercent(config->namcoES1.ffbBaseDamper);
    state.springDeadband = static_cast<float>(config->namcoES1.ffbDeadband) / 1000.0f;
    state.vibrationStrength = vibration;
    state.vibrationPeriodMs = period > 0 ? period : 20;
    state.vibrationDurationMs = 20;
    sdlFfbApplySteering(&state);

    if (config->namcoES1.forceFeedbackDiagnostics &&
        (++g_sequence == 1 || g_sequence % 120 == 0))
        log_info("System ES1 WMMT4 FFB[%lu]: torque=%.3f spring=%.3f damper=%.3f "
                 "vibration=%.3f period=%d",
                 g_sequence, torque, spring, damper, vibration, period);
}

void publishOutputState(float force, float spring, float damper, float vibration,
                        bool oneShot)
{
    const EmulatorConfig *config = getConfig();
    if (!config->namcoES1.forceFeedbackEnabled || !g_powerActive || !g_running)
        return;

    /* Ignore stale loading torque until several centred updates arrive. */
    if (!g_outputArmed)
    {
        const float absoluteForce = force < 0.0f ? -force : force;
        if (absoluteForce < NeutralForceThreshold)
            ++g_neutralFrames;
        else
            g_neutralFrames = 0;

        if (g_neutralFrames < NeutralFramesRequired)
        {
            disableHostFfb();
            return;
        }
        g_outputArmed = true;
    }

    const float gain = scalePercent(config->namcoES1.ffbGain);
    const float weight = scalePercent(config->namcoES1.ffbWeight);
    const float springGain = scalePercent(config->namcoES1.ffbSpringGain);
    const float damperGain = scalePercent(config->namcoES1.ffbDamperGain);
    const float vibrationGain = scalePercent(config->namcoES1.ffbVibrationGain);

    float outputForce = force * gain * weight;
    float outputVibration = vibration * vibrationGain;

    if (oneShot)
    {
        /* Expand one-shot values and merge vibration into the impact impulse. */
        outputForce *= 2.0f;
        const float forceMagnitude = absoluteValue(outputForce);
        const float vibrationMagnitude = absoluteValue(outputVibration);
        if (vibrationMagnitude > forceMagnitude)
            outputForce = outputForce < 0.0f ? -vibrationMagnitude : vibrationMagnitude;
        outputForce = clampSigned(outputForce);
        outputVibration = clampSigned(outputVibration);
    }

    if (config->namcoES1.ffbInvert)
        outputForce = -outputForce;

    FfbSteeringState state = {};
    state.enabled = 1;
    state.constantForce = outputForce;
    state.springStrength = spring * springGain;
    state.damperStrength = damper * damperGain;
    state.damperFloor = scalePercent(config->namcoES1.ffbBaseDamper);
    state.springDeadband = static_cast<float>(config->namcoES1.ffbDeadband) / 1000.0f;
    state.vibrationStrength = outputVibration;
    /* Keep transient vibration finite so it cannot persist across transitions. */
    state.vibrationPeriodMs = 20;
    state.vibrationDurationMs = oneShot
                                    ? (config->namcoES1.ffbRumbleDuration > 0
                                           ? config->namcoES1.ffbRumbleDuration
                                           : 100)
                                    : 20;
    sdlFfbApplySteering(&state);

    if (config->namcoES1.forceFeedbackDiagnostics &&
        (++g_sequence == 1 || g_sequence % 120 == 0))
        log_info("System ES1 WMMT4 FFB output[%lu]: force=%.3f spring=%.3f "
                 "damper=%.3f vibration=%.3f oneShot=%d armed=%d",
                 g_sequence, outputForce, spring, damper, outputVibration,
                 oneShot ? 1 : 0,
                 g_outputArmed ? 1 : 0);
}

extern "C" void wmmt4FfbSetTorque(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalTorque)
        g_originalTorque(object, value);
    publish(object);
}

extern "C" void wmmt4FfbApplyOutput(void *object, float force, float spring,
                                      float damper, float vibration)
{
    GuestTls::HostCallScope hostCall;
    const bool oneShot = g_oneShotPending;
    g_oneShotPending = false;
    if (g_originalApplyFfb)
        g_originalApplyFfb(object, force, spring, damper, vibration);
    publishOutputState(force, spring, damper, vibration, oneShot);
}

extern "C" void wmmt4FfbSetOneShotEffect(void *object, float force, float spring,
                                           float damper, float vibration)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalSetOneShotEffect)
        g_originalSetOneShotEffect(object, force, spring, damper, vibration);
    g_oneShotPending = true;
}

extern "C" void wmmt4FfbSetPeriod(void *object, int value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalPeriod)
        g_originalPeriod(object, value);
    publish(object);
}

extern "C" void wmmt4FfbSetSpring(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalSpring)
        g_originalSpring(object, value);
    publish(object);
}

extern "C" void wmmt4FfbSetDamper(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalDamper)
        g_originalDamper(object, value);
    publish(object);
}

extern "C" void wmmt4FfbSetVibrate(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalVibrate)
        g_originalVibrate(object, value);
    publish(object);
}

extern "C" void wmmt4FfbPowerOn(void *object, int running)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalPowerOn)
        g_originalPowerOn(object, running);
    g_powerActive = true;
    g_running = running != 0;
    g_outputArmed = false;
    g_oneShotPending = false;
    g_neutralFrames = 0;
    es1KickbackReportMotorPower(1);
}

extern "C" void wmmt4FfbPowerOff(void *object, int running)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalPowerOff)
        g_originalPowerOff(object, running);
    g_powerActive = false;
    g_running = false;
    g_outputArmed = false;
    g_oneShotPending = false;
    g_neutralFrames = 0;
    disableHostFfb();
    es1KickbackReportMotorPower(0);
}

extern "C" void wmmt4FfbStartSelfCheck(void *object)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalStartSelfCheck)
        g_originalStartSelfCheck(object);
    completeVirtualSelfCheck(object);
    es1KickbackReportSelfCheck();
}
} // namespace

extern "C" int wmmt4InstallFfbHooks(void)
{
    if (!getConfig()->namcoES1.forceFeedbackEnabled)
        return 0;

    const Es1HookSpec hooks[] = {
        {ApplyFfbAddress, reinterpret_cast<void *>(wmmt4FfbApplyOutput), "FFB apply output",
         reinterpret_cast<void **>(&g_originalApplyFfb)},
        {SetOneShotEffectAddress, reinterpret_cast<void *>(wmmt4FfbSetOneShotEffect),
         "FFB one-shot effect", reinterpret_cast<void **>(&g_originalSetOneShotEffect)},
        {SetTorqueAddress, reinterpret_cast<void *>(wmmt4FfbSetTorque), "FFB setTorque",
         reinterpret_cast<void **>(&g_originalTorque)},
        {SetPeriodAddress, reinterpret_cast<void *>(wmmt4FfbSetPeriod), "FFB setPeriod",
         reinterpret_cast<void **>(&g_originalPeriod)},
        {SetSpringAddress, reinterpret_cast<void *>(wmmt4FfbSetSpring), "FFB setSpring",
         reinterpret_cast<void **>(&g_originalSpring)},
        {SetDamperAddress, reinterpret_cast<void *>(wmmt4FfbSetDamper), "FFB setDamper",
         reinterpret_cast<void **>(&g_originalDamper)},
        {SetVibrateAddress, reinterpret_cast<void *>(wmmt4FfbSetVibrate), "FFB setVibrate",
         reinterpret_cast<void **>(&g_originalVibrate)},
        {PowerOnAddress, reinterpret_cast<void *>(wmmt4FfbPowerOn), "FFB power on",
         reinterpret_cast<void **>(&g_originalPowerOn)},
        {PowerOffAddress, reinterpret_cast<void *>(wmmt4FfbPowerOff), "FFB power off",
         reinterpret_cast<void **>(&g_originalPowerOff)},
        {StartSelfCheckAddress, reinterpret_cast<void *>(wmmt4FfbStartSelfCheck),
         "FFB start self check", reinterpret_cast<void **>(&g_originalStartSelfCheck)},
    };
    return es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
}
