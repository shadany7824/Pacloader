#include "n2.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include "../../../config/config.h"
#include "../../../input/sdlInput.h"

#include <algorithm>
#include <cstdint>

namespace
{
int currentGear = 0;
bool previousGearUp = false;
bool previousGearDown = false;

uint16_t n2GearSwitches(int gear)
{
    /* The shifter is four switches in the first JVS player word, BUTTON_3
     * through BUTTON_6.  These are the masks the JVIO board reports. */
    switch (gear)
    {
        case 1: return 0x20 | 0x80;
        case 2: return 0x20 | 0x40;
        case 3: return 0x80;
        case 4: return 0x40;
        case 5: return 0x10 | 0x80;
        case 6: return 0x10 | 0x40;
        default: return 0;
    }
}

float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

/*
 * What the potentiometers put on the wire.  The pedals swing across the window
 * the game calibrates them in; the wheel has more travel than its window, so it
 * gets the whole 16-bit range and the game's calibration places full lock.
 */
uint16_t calibratedRaw(float normalized, int rawMin, int rawMax)
{
    rawMin = std::max(0, std::min(rawMin, 65535));
    rawMax = std::max(0, std::min(rawMax, 65535));
    // A reversed or collapsed pair would silently pin the axis, so the ends are
    // put back in order rather than trusted as configured.
    if (rawMax < rawMin)
        std::swap(rawMin, rawMax);

    /* Rounded, not truncated: a pedal a hair short of 1.0 otherwise reads one
     * count low across its whole travel. */
    const float raw = static_cast<float>(rawMin) +
                      clamp01(normalized) * static_cast<float>(rawMax - rawMin) + 0.5f;
    return static_cast<uint16_t>(std::min(raw, 65535.0f));
}

bool switchActive(const JVSIO *io, JVSPlayer player, JVSInput input)
{
    return io && (io->state.inputSwitch[player] & input) != 0;
}
} // namespace

/*
 * Advances the six-position shifter from the GearUp/GearDown bindings, or
 * reports 0 while the raw shifter switches are thrown directly.  Both the
 * direct-write path and the JVS bridge drive this one state machine.
 */
extern "C" int n2UpdateShifter(void)
{
    JVSIO *io = getJVSIO();
    // Only six shifter positions have switch patterns, so a controls.ini asking
    // for more gears than the cabinet has is capped rather than refused.
    const int topGear = std::max(1, std::min(getShifterGears(), 6));

    const bool gearUp = switchActive(io, PLAYER_2, BUTTON_UP);
    const bool gearDown = switchActive(io, PLAYER_2, BUTTON_DOWN);
    if (gearUp && !previousGearUp && currentGear < topGear)
        ++currentGear;
    if (gearDown && !previousGearDown && currentGear > 0)
        --currentGear;
    previousGearUp = gearUp;
    previousGearDown = gearDown;

    // A bound H-pattern position beats the paddles, and stays latched on
    // release: keyboard bindings work, and a shifter between gates is not
    // read as neutral.
    const int directGear = getWmmtDirectGear();
    if (directGear > 0)
        currentGear = directGear;

    const bool manualShifterSwitch =
        switchActive(io, PLAYER_1, BUTTON_3) || switchActive(io, PLAYER_1, BUTTON_4) ||
        switchActive(io, PLAYER_1, BUTTON_5) || switchActive(io, PLAYER_1, BUTTON_6);
    if (manualShifterSwitch)
        currentGear = 0;

    return currentGear;
}

extern "C" uint16_t n2GearSwitchBits(int gear)
{
    return n2GearSwitches(gear);
}

extern "C" uint16_t n2AnalogueCount(int channel, float normalized)
{
    const EmulatorConfig *config = getConfig();
    switch (channel)
    {
        case N2_ANALOGUE_STEERING:
            return calibratedRaw(normalized, config->namcoN2.steering.minimum,
                                 config->namcoN2.steering.maximum);
        case N2_ANALOGUE_ACCELERATOR:
            return calibratedRaw(normalized, config->namcoN2.accelerator.minimum,
                                 config->namcoN2.accelerator.maximum);
        case N2_ANALOGUE_BRAKE:
            return calibratedRaw(normalized, config->namcoN2.brake.minimum,
                                 config->namcoN2.brake.maximum);
        default: return 0;
    }
}

#endif
