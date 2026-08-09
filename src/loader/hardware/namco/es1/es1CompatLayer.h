#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/* Common primitives shared by title-specific ES1 adapters. */
struct Es1HookSpec
{
    uintptr_t address;
    void *replacement;
    const char *name;
    void **original = nullptr;
};

int es1InstallHookTable(const Es1HookSpec *hooks, size_t count, const char *title);

uint32_t es1CompatCrc32Mpeg(const uint8_t *data, size_t length);
int es1CompatReadBlob(const uint8_t *blob, size_t blobSize, int offset, int length,
                      uint8_t *buffer);
int es1CompatWriteBlob(uint8_t *blob, size_t blobSize, int offset, int length,
                       const uint8_t *buffer);
bool es1CompatGzipDecompress(const uint8_t *data, size_t size,
                             std::vector<uint8_t> &output);

struct Es1JvioInputProfile
{
    uint16_t interruptSwitch;
    uint16_t viewChangeSwitch;
    uint16_t serviceSwitch;
    uint16_t testEnterSwitch;
    uint16_t testDownSwitch;
    uint16_t testUpSwitch;
    uint16_t terminalSwitch;
    uint16_t gearMasks[7];
    uint8_t testSwitchActiveValue;
    uint8_t steeringInput;
    uint8_t gasInput;
    uint8_t brakeInput;
};

struct Es1JvioInputState
{
    int sequentialGear = 0;
    bool previousTest = false;
    bool previousGearUp = false;
    bool previousGearDown = false;
    bool previousCoin = false;
    int coinCount = 0;
};

void es1CompatUpdateJvioInput(uint8_t *testSwitch, uint16_t *switches,
                              uint16_t analogueInputs[16], uint16_t *coin,
                              const Es1JvioInputProfile &profile,
                              Es1JvioInputState &state);
