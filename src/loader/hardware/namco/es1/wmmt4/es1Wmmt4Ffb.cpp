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
/*
 * WMMT4's FFB entry points, each with the opening bytes found there. The game
 * carries no symbols for its own code, so these can only be addresses - but an
 * address alone cannot say whether it still points at the function it named.
 * The signature turns a wrong build into a refused hook and a log line instead
 * of a jump into unrelated code. Taken from WMN4r Rev 1.10.18.
 */
struct FfbTarget
{
    uintptr_t address;
    uint8_t signature[16];
};

constexpr FfbTarget SetTorqueTarget = /* float, -1..1 */
    {0x8367ae0, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xd9, 0x45,
                 0x0c, 0xd9, 0xe8, 0xd9, 0xe0, 0xdb, 0xe9, 0xc6}};
constexpr FfbTarget SetPeriodTarget = /* int */
    {0x8367b10, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x8b, 0x55,
                 0x0c, 0xc6, 0x40, 0x21, 0x01, 0x89, 0x50, 0x2c}};
constexpr FfbTarget SetSpringTarget = /* float, 0..1 */
    {0x8367b30, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xd9, 0x45,
                 0x0c, 0xd9, 0xee, 0xdb, 0xe9, 0xc6, 0x40, 0x22}};
constexpr FfbTarget SetDamperTarget = /* float, 0..1 */
    {0x8367b60, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xd9, 0x45,
                 0x0c, 0xd9, 0xee, 0xdb, 0xe9, 0xc6, 0x40, 0x23}};
constexpr FfbTarget SetVibrateTarget = /* float, 0..1 */
    {0x8367b90, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xd9, 0x45,
                 0x0c, 0xd9, 0xe8, 0xd9, 0xe0, 0xdb, 0xe9, 0xc6}};
/* Per-frame dispatch: force, spring, damper and vibration floats. */
constexpr FfbTarget ApplyFfbTarget =
    {0x8366e30, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x8b, 0x40,
                 0x04, 0x89, 0x45, 0x08, 0x5d, 0xe9, 0x8e, 0xfb}};
/* Wall-hit effects are staged here before the update loop dispatches them. */
constexpr FfbTarget SetOneShotEffectTarget =
    {0x8367ce0, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x8b, 0x55,
                 0x0c, 0xc6, 0x40, 0x3c, 0x01, 0x89, 0x50, 0x40}};
constexpr FfbTarget PowerOnTarget =
    {0x8366f80, {0x8b, 0x0d, 0x70, 0x53, 0x1e, 0x09, 0x55, 0x89,
                 0xe5, 0x8b, 0x55, 0x0c, 0x85, 0xc9, 0x74, 0x1e}};
constexpr FfbTarget PowerOffTarget =
    {0x8366fb0, {0xa1, 0x70, 0x53, 0x1e, 0x09, 0x55, 0x89, 0xe5,
                 0x8b, 0x55, 0x0c, 0x85, 0xc0, 0x74, 0x1a, 0x8b}};
constexpr FfbTarget StartSelfCheckTarget =
    {0x8367ee0, {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x8b, 0x45,
                 0x08, 0xc6, 0x40, 0x50, 0x01, 0x8b, 0x40, 0x04}};
/* Called directly rather than hooked, so it is verified at the call site. */
constexpr FfbTarget CompleteDriverSelfCheckTarget =
    {0x8366230, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xc6, 0x40,
                 0x40, 0x00, 0xc7, 0x40, 0x08, 0x70, 0x61, 0x36}};

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

    /* Complete the title-side self-check and clear its pending flag. Verified
     * here because this one is called, not hooked, so nothing else checks it. */
    if (!es1VerifyGuestCode(CompleteDriverSelfCheckTarget.address,
                            CompleteDriverSelfCheckTarget.signature,
                            sizeof(CompleteDriverSelfCheckTarget.signature),
                            "FFB complete driver self check", "WMMT4"))
        return;

    GuestTls::EnterGuestCode();
    reinterpret_cast<CompleteDriverSelfCheck>(CompleteDriverSelfCheckTarget.address)(driver);
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
    es1KickbackReportMotorPower(1);
    if (g_originalPowerOn)
        g_originalPowerOn(object, running);
    g_powerActive = true;
    g_running = running != 0;
    g_outputArmed = false;
    g_oneShotPending = false;
    g_neutralFrames = 0;
}

extern "C" void wmmt4FfbPowerOff(void *object, int running)
{
    GuestTls::HostCallScope hostCall;
    es1KickbackReportMotorPower(0);
    if (g_originalPowerOff)
        g_originalPowerOff(object, running);
    g_powerActive = false;
    g_running = false;
    g_outputArmed = false;
    g_oneShotPending = false;
    g_neutralFrames = 0;
    disableHostFfb();
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

#define FFB_HOOK(target, replacement, label, original)                                   \
    {                                                                                    \
        (target).address, reinterpret_cast<void *>(replacement), (label),                \
            reinterpret_cast<void **>(original), (target).signature,                     \
            sizeof((target).signature)                                                   \
    }

    const Es1HookSpec hooks[] = {
        FFB_HOOK(ApplyFfbTarget, wmmt4FfbApplyOutput, "FFB apply output", &g_originalApplyFfb),
        FFB_HOOK(SetOneShotEffectTarget, wmmt4FfbSetOneShotEffect, "FFB one-shot effect",
                 &g_originalSetOneShotEffect),
        FFB_HOOK(SetTorqueTarget, wmmt4FfbSetTorque, "FFB setTorque", &g_originalTorque),
        FFB_HOOK(SetPeriodTarget, wmmt4FfbSetPeriod, "FFB setPeriod", &g_originalPeriod),
        FFB_HOOK(SetSpringTarget, wmmt4FfbSetSpring, "FFB setSpring", &g_originalSpring),
        FFB_HOOK(SetDamperTarget, wmmt4FfbSetDamper, "FFB setDamper", &g_originalDamper),
        FFB_HOOK(SetVibrateTarget, wmmt4FfbSetVibrate, "FFB setVibrate", &g_originalVibrate),
        FFB_HOOK(PowerOnTarget, wmmt4FfbPowerOn, "FFB power on", &g_originalPowerOn),
        FFB_HOOK(PowerOffTarget, wmmt4FfbPowerOff, "FFB power off", &g_originalPowerOff),
        FFB_HOOK(StartSelfCheckTarget, wmmt4FfbStartSelfCheck, "FFB start self check",
                 &g_originalStartSelfCheck),
    };
#undef FFB_HOOK
    return es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
}
