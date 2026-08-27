#include "es1Wmmt4Ffb.hpp"
#include "../es1Kickback.h"
#include "../es1CompatLayer.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../ffb/sdlFfbBackend.h"
#include "../../../../config/config.h"
#include "../../../../log/log.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
/* Verified entry signatures for WMN4r Rev 1.10.18. */
struct FfbTarget
{
    uintptr_t address;
    uint8_t signature[16];
};

constexpr FfbTarget SetTorqueTarget = /* float, -1..1 */
    {0x8367ae0, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xd9, 0x45,
                 0x0c, 0xd9, 0xe8, 0xd9, 0xe0, 0xdb, 0xe9, 0xc6}};
/* The update routine adds this field to torque as an IEEE-754 float. */
constexpr FfbTarget SetTorqueBiasTarget = /* float */
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
constexpr FfbTarget PowerOnEntryTarget =
    {0x8367aa0, {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x0f, 0xb6,
                 0x45, 0x0c, 0x89, 0x45, 0x0c, 0x8b, 0x45, 0x08}};
constexpr FfbTarget PowerOffEntryTarget =
    {0x8367ac0, {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x0f, 0xb6,
                 0x45, 0x0c, 0x89, 0x45, 0x0c, 0x8b, 0x45, 0x08}};
constexpr FfbTarget StartSelfCheckTarget =
    {0x8367ee0, {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x8b, 0x45,
                 0x08, 0xc6, 0x40, 0x50, 0x01, 0x8b, 0x40, 0x04}};
/* Called directly rather than hooked, so it is verified at the call site. */
constexpr FfbTarget CompleteDriverSelfCheckTarget =
    {0x8366230, {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xc6, 0x40,
                 0x40, 0x00, 0xc7, 0x40, 0x08, 0x70, 0x61, 0x36}};

/* Field offsets in that object, in the order the setters write them. */
constexpr size_t TorqueOffset = 0x28;
constexpr size_t TorqueBiasOffset = 0x2c;
constexpr size_t SpringOffset = 0x30;
constexpr size_t DamperOffset = 0x34;
constexpr size_t VibrateOffset = 0x38;

using SetFloat = void (*)(void *, float);
using ApplyFfb = void (*)(void *, float, float, float, float);
using SetPower = void (*)(void *, int);
using StartSelfCheck = void (*)(void *);
using CompleteDriverSelfCheck = int (*)(void *);

SetFloat g_originalTorque = nullptr;
SetFloat g_originalTorqueBias = nullptr;
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

    /* Complete the title-side self-check through its verified call site. */
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

constexpr unsigned int ResumeFramesRequired = 3;
constexpr float LiveOutputThreshold = 0.001f;

std::atomic<unsigned long> g_sequence{0};
std::atomic<bool> g_powerActive{false};
std::atomic<bool> g_running{false};
std::atomic<void *> g_savedFfbObject{nullptr};
bool g_oneShotPending = false;
unsigned int g_resumeCandidates = 0;

float scalePercent(int value)
{
    if (value < 0)
        value = 0;
    if (value > 200)
        value = 200;
    return static_cast<float>(value) / 100.0f;
}

float absoluteValue(float value)
{
    return value < 0.0f ? -value : value;
}

bool hasLiveOutput(float force, float spring, float damper, float vibration)
{
    return absoluteValue(force) > LiveOutputThreshold ||
           absoluteValue(spring) > LiveOutputThreshold ||
           absoluteValue(damper) > LiveOutputThreshold ||
           absoluteValue(vibration) > LiveOutputThreshold;
}

void rememberFfbObject(const void *object)
{
    if (object)
        g_savedFfbObject.store(const_cast<void *>(object), std::memory_order_release);
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

/* Read the complete title-side object after setter calls. */
void publishFfbObject(const void *object)
{
    const EmulatorConfig *config = getConfig();
    if (!object || !config->namcoES1.forceFeedbackEnabled)
        return;

    rememberFfbObject(object);

    /* Suppress stale object values during menus and self-test. */
    if (!g_powerActive || !g_running)
    {
        disableHostFfb();
        return;
    }

    float torque = (readFloat(object, TorqueOffset) + readFloat(object, TorqueBiasOffset)) *
                   scalePercent(config->namcoES1.ffbGain) *
                   scalePercent(config->namcoES1.ffbWeight);
    if (config->namcoES1.ffbInvert)
        torque = -torque;
    const float spring = readFloat(object, SpringOffset) *
                         scalePercent(config->namcoES1.ffbSpringGain);
    const float damper = readFloat(object, DamperOffset) *
                         scalePercent(config->namcoES1.ffbDamperGain);
    const float vibration = readFloat(object, VibrateOffset) *
                            scalePercent(config->namcoES1.ffbVibrationGain);

    FfbSteeringState state = {};
    state.enabled = 1;
    state.constantForce = torque;
    state.springStrength = spring;
    state.damperStrength = damper;
    state.damperFloor = scalePercent(config->namcoES1.ffbBaseDamper);
    state.springDeadband = static_cast<float>(config->namcoES1.ffbDeadband) / 1000.0f;
    state.vibrationStrength = vibration;
    state.vibrationPeriodMs = 20;
    state.vibrationDurationMs = 20;
    sdlFfbApplySteering(&state);
}

/* Forward the title's authoritative per-frame output to the host backend. */
void publishFfbOutput(float force, float spring, float damper, float vibration,
                      bool oneShot)
{
    const EmulatorConfig *config = getConfig();
    if (!config->namcoES1.forceFeedbackEnabled)
        return;

    if (!g_powerActive || !g_running)
    {
        if (!hasLiveOutput(force, spring, damper, vibration))
        {
            g_resumeCandidates = 0;
            return;
        }

        if (++g_resumeCandidates < ResumeFramesRequired)
        {
            disableHostFfb();
            return;
        }

        g_powerActive = true;
        g_running = true;
        if (config->namcoES1.forceFeedbackDiagnostics)
            log_info("System ES1 WMMT4 FFB: implicit resume from live output");
    }

    g_resumeCandidates = 0;

    const float gain = scalePercent(config->namcoES1.ffbGain);
    const float weight = scalePercent(config->namcoES1.ffbWeight);
    const float springGain = scalePercent(config->namcoES1.ffbSpringGain);
    const float damperGain = scalePercent(config->namcoES1.ffbDamperGain);
    const float vibrationGain = scalePercent(config->namcoES1.ffbVibrationGain);

    float outputForce = force * gain * weight;
    const float outputVibration = vibration * vibrationGain;

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

    const unsigned long sequence = ++g_sequence;
    if (config->namcoES1.forceFeedbackDiagnostics &&
        (sequence == 1 || sequence % 120 == 0))
        log_info("System ES1 WMMT4 FFB[%lu]: force=%.3f spring=%.3f damper=%.3f "
                 "vibration=%.3f oneShot=%d",
                 sequence, outputForce, spring, damper, outputVibration,
                 oneShot ? 1 : 0);
}

void wmmt4FfbPoll(void)
{
    if (!g_powerActive || !g_running)
        return;

    void *object = g_savedFfbObject.load(std::memory_order_acquire);
    if (!object)
        return;

    publishFfbObject(object);
    sdlFfbReapplySteering();
}

extern "C" void wmmt4FfbSetTorque(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalTorque)
        g_originalTorque(object, value);
    rememberFfbObject(object);
}

extern "C" void wmmt4FfbApplyOutput(void *object, float force, float spring,
                                      float damper, float vibration)
{
    GuestTls::HostCallScope hostCall;
    const bool oneShot = g_oneShotPending;
    g_oneShotPending = false;
    if (g_originalApplyFfb)
        g_originalApplyFfb(object, force, spring, damper, vibration);
    publishFfbOutput(force, spring, damper, vibration, oneShot);
}

extern "C" void wmmt4FfbSetOneShotEffect(void *object, float force, float spring,
                                           float damper, float vibration)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalSetOneShotEffect)
        g_originalSetOneShotEffect(object, force, spring, damper, vibration);
    rememberFfbObject(object);
    g_oneShotPending = true;
}

extern "C" void wmmt4FfbSetTorqueBias(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalTorqueBias)
        g_originalTorqueBias(object, value);
    rememberFfbObject(object);
}

extern "C" void wmmt4FfbSetSpring(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalSpring)
        g_originalSpring(object, value);
    rememberFfbObject(object);
}

extern "C" void wmmt4FfbSetDamper(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalDamper)
        g_originalDamper(object, value);
    rememberFfbObject(object);
}

extern "C" void wmmt4FfbSetVibrate(void *object, float value)
{
    GuestTls::HostCallScope hostCall;
    if (g_originalVibrate)
        g_originalVibrate(object, value);
    rememberFfbObject(object);
}

extern "C" void wmmt4FfbPowerOn(void *object, int running)
{
    GuestTls::HostCallScope hostCall;
    if (getConfig()->namcoES1.serialDiagnostics)
        log_info("System ES1 steering diag: FFB PowerOn object=%p running=%d", object, running);
    rememberFfbObject(object);
    es1KickbackReportMotorPower(1);
    if (g_originalPowerOn)
        g_originalPowerOn(object, running);
    const bool active = running != 0;
    g_powerActive = true;
    g_running = active;
    g_oneShotPending = false;
    g_resumeCandidates = 0;

    if (active)
        publishFfbObject(object);
}

extern "C" void wmmt4FfbPowerOff(void *object, int running)
{
    GuestTls::HostCallScope hostCall;
    if (getConfig()->namcoES1.serialDiagnostics)
        log_info("System ES1 steering diag: FFB PowerOff object=%p running=%d", object, running);
    es1KickbackReportMotorPower(0);
    if (g_originalPowerOff)
        g_originalPowerOff(object, running);
    g_powerActive = false;
    g_running = false;
    g_oneShotPending = false;
    g_resumeCandidates = 0;
    g_savedFfbObject.store(nullptr, std::memory_order_release);
    disableHostFfb();
}

extern "C" void wmmt4FfbStartSelfCheck(void *object)
{
    GuestTls::HostCallScope hostCall;
    if (getConfig()->namcoES1.serialDiagnostics)
        log_info("System ES1 steering diag: FFB StartSelfCheck object=%p", object);
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
        FFB_HOOK(SetTorqueBiasTarget, wmmt4FfbSetTorqueBias, "FFB set torque bias",
                 &g_originalTorqueBias),
        FFB_HOOK(SetSpringTarget, wmmt4FfbSetSpring, "FFB setSpring", &g_originalSpring),
        FFB_HOOK(SetDamperTarget, wmmt4FfbSetDamper, "FFB setDamper", &g_originalDamper),
        FFB_HOOK(SetVibrateTarget, wmmt4FfbSetVibrate, "FFB setVibrate", &g_originalVibrate),
        FFB_HOOK(PowerOnEntryTarget, wmmt4FfbPowerOn, "FFB power on", &g_originalPowerOn),
        FFB_HOOK(PowerOffEntryTarget, wmmt4FfbPowerOff, "FFB power off", &g_originalPowerOff),
        FFB_HOOK(StartSelfCheckTarget, wmmt4FfbStartSelfCheck, "FFB start self check",
                 &g_originalStartSelfCheck),
    };
#undef FFB_HOOK
    const int installed =
        es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4");
    if (installed > 0)
        sdlFfbSetSteeringPoll(wmmt4FfbPoll);
    return installed;
}
