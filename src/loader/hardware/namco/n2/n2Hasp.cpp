#include "n2Hasp.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include "n2Hook.h"

#include "../../../config/config.h"
#include "../../../elfLoader/symbolResolver.hpp"
#include "../../../log/log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace
{
uint32_t nextHaspHandle = 1;
constexpr int n2HaspDataSize = 0xD40;
constexpr char defaultN2DongleId[] = "000001000001";
// A cabinet never carries two dongles with the same serial, and the attract
// screen prints both, so the fallbacks have to differ.
constexpr char defaultN2DongleId2[] = "000001000002";
uint8_t n2HaspData[n2HaspDataSize] = {};
bool n2HaspDataInitialized = false;

int returnSuccess()
{
    return 1;
}

int returnHaspSuccess()
{
    return 0;
}
int getN2HaspCount()
{
    return 2;
}

bool isValidN2DongleId(const char *dongleId)
{
    if (!dongleId || std::strlen(dongleId) != 12)
        return false;

    return std::all_of(dongleId, dongleId + 12, [](unsigned char value) {
        return value >= '0' && value <= '9';
    });
}

void setHaspRecordChecksum(uint8_t *record)
{
    uint8_t checksum = 0;
    for (int i = 0; i < 14; ++i)
        checksum = static_cast<uint8_t>(checksum + record[i]);
    record[14] = checksum;
    record[15] = static_cast<uint8_t>(checksum ^ 0xFF);
}

void initializeN2HaspData()
{
    if (n2HaspDataInitialized)
        return;

    std::memset(n2HaspData, 0, sizeof(n2HaspData));

    /*
     * clHasp2 expects a checksum-protected 16-byte card-state record at
     * offset zero.  An all-zero payload represents zero stored card IDs.
     */
    setHaspRecordChecksum(n2HaspData);

    const char *configuredId = getConfig()->namcoN2.dongleId;
    const char *dongleId = isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId;
    std::memcpy(n2HaspData + 0xD00, dongleId, 12);

    /*
     * The 64-byte identity block stores its checksum and one's complement
     * in the final two bytes.
     */
    uint8_t checksum = 0;
    for (int i = 0; i < 0x3E; ++i)
        checksum = static_cast<uint8_t>(checksum + n2HaspData[0xD00 + i]);
    n2HaspData[0xD3E] = checksum;
    n2HaspData[0xD3F] = static_cast<uint8_t>(checksum ^ 0xFF);

    n2HaspDataInitialized = true;
    log_info("Namco N2: virtual USB dongle initialized (S/N %.6s-%.6s)",
             dongleId, dongleId + 6);
}

uint32_t parseDongleSerialPart(const char *digits)
{
    uint32_t value = 0;
    for (int i = 0; i < 6; ++i)
        value = value * 10 + static_cast<uint32_t>(digits[i] - '0');
    return value;
}

const char *getN2DongleId()
{
    const char *configuredId = getConfig()->namcoN2.dongleId;
    return isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId;
}

uint32_t getN2DongleSerialHi()
{
    return parseDongleSerialPart(getN2DongleId());
}

uint32_t getN2DongleSerialLo()
{
    return parseDongleSerialPart(getN2DongleId() + 6);
}

const char *getN2Dongle2Id()
{
    const char *configuredId = getConfig()->namcoN2.dongleId2;
    return isValidN2DongleId(configuredId) ? configuredId : defaultN2DongleId2;
}

uint32_t getN2Dongle2SerialHi()
{
    return parseDongleSerialPart(getN2Dongle2Id());
}

uint32_t getN2Dongle2SerialLo()
{
    return parseDongleSerialPart(getN2Dongle2Id() + 6);
}

void openN2Hasp2(void *object)
{
    if (!object)
        return;

    uint8_t *state = *reinterpret_cast<uint8_t **>(object);
    if (!state)
        return;

    initializeN2HaspData();
    *reinterpret_cast<int32_t *>(state + 0x00) = 0;
    *reinterpret_cast<uint32_t *>(state + 0x04) = nextHaspHandle++;
    *reinterpret_cast<uint32_t *>(state + 0x08) = getN2Dongle2SerialHi();
    *reinterpret_cast<uint32_t *>(state + 0x0C) = getN2Dongle2SerialLo();
    std::memcpy(state + 0x10, n2HaspData, 16);
}

void openN2Hasp(void *object)
{
    if (!object)
        return;

    uint8_t *state = *reinterpret_cast<uint8_t **>(object);
    if (!state)
        return;

    initializeN2HaspData();
    *reinterpret_cast<int32_t *>(state + 0x00) = 0;
    *reinterpret_cast<uint32_t *>(state + 0x04) = nextHaspHandle++;
    state[0x08] = 0; // low-battery flag
    *reinterpret_cast<uint32_t *>(state + 0x0C) = getN2DongleSerialHi();
    *reinterpret_cast<uint32_t *>(state + 0x10) = getN2DongleSerialLo();
    std::memcpy(state + 0x14, n2HaspData, 16);
}

void testN2Hasp2(void *object)
{
    openN2Hasp2(object);
}

void testN2Hasp(void *object)
{
    openN2Hasp(object);
}
int haspLogin(int, int, uint32_t *handle)
{
    initializeN2HaspData();
    if (handle)
        *handle = nextHaspHandle++;
    return 0;
}

int haspGetSize(int, int, int *size)
{
    if (size)
        *size = 0xD40;
    return 0;
}

int haspRead(int, int, int offset, int length, uint8_t *buffer)
{
    if (!buffer || offset < 0 || length < 0 || offset > n2HaspDataSize ||
        length > n2HaspDataSize - offset)
        return 1;

    initializeN2HaspData();
    std::memcpy(buffer, n2HaspData + offset, static_cast<size_t>(length));
    return 0;
}

int haspWrite(int, int, int offset, int length, const uint8_t *buffer)
{
    if (!buffer || offset < 0 || length < 0 || offset > n2HaspDataSize ||
        length > n2HaspDataSize - offset)
        return 1;

    initializeN2HaspData();
    std::memcpy(n2HaspData + offset, buffer, static_cast<size_t>(length));
    return 0;
}
} // namespace

extern "C" void n2RegisterHaspPreloadOverrides(void)
{
    SymbolResolver::GetInstance().RegisterVTable("hasp_login", reinterpret_cast<void *>(haspLogin));
    SymbolResolver::GetInstance().RegisterVTable("hasp_logout", reinterpret_cast<void *>(returnHaspSuccess));
    SymbolResolver::GetInstance().RegisterVTable("hasp_get_size", reinterpret_cast<void *>(haspGetSize));
    SymbolResolver::GetInstance().RegisterVTable("hasp_read", reinterpret_cast<void *>(haspRead));
    SymbolResolver::GetInstance().RegisterVTable("hasp_write", reinterpret_cast<void *>(haspWrite));
    SymbolResolver::GetInstance().RegisterVTable("hasp_encrypt", reinterpret_cast<void *>(returnHaspSuccess));
    SymbolResolver::GetInstance().RegisterVTable("hasp_decrypt", reinterpret_cast<void *>(returnHaspSuccess));
    SymbolResolver::GetInstance().RegisterVTable("hasp_cleanup", reinterpret_cast<void *>(returnHaspSuccess));
}

extern "C" int n2HaspInstallHooks(void)
{
    n2HookSymbol("hasp_cleanup", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_decrypt", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_encrypt", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_free", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_rtc", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_sessioninfo", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_get_size", reinterpret_cast<void *>(haspGetSize));
    n2HookSymbol("hasp_login", reinterpret_cast<void *>(haspLogin));
    n2HookSymbol("hasp_logout", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("hasp_read", reinterpret_cast<void *>(haspRead));
    n2HookSymbol("hasp_write", reinterpret_cast<void *>(haspWrite));
    n2HookSymbol("_ZNK6clHasp7isAvailEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK7clHasp27isAvailEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK6clHasp8getCountEv", reinterpret_cast<void *>(getN2HaspCount));
    n2HookSymbol("_ZNK7clHasp28getCountEv", reinterpret_cast<void *>(getN2HaspCount));
    /*
     * Both classes probe /proc/bus/usb/devices inside open(), before calling
     * the HASP API.  Windows has no such procfs entry, so initialize the
     * already-constructed x86 class state directly.
     */
    n2HookSymbol("_ZN6clHasp4openEv", reinterpret_cast<void *>(openN2Hasp));
    n2HookSymbol("_ZN7clHasp24openEv", reinterpret_cast<void *>(openN2Hasp2));
    n2HookSymbol("_ZN6clHasp4testEv", reinterpret_cast<void *>(testN2Hasp));
    n2HookSymbol("_ZN7clHasp24testEv", reinterpret_cast<void *>(testN2Hasp2));
    n2HookSymbol("_ZNK6clHaspcvbEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK7clHasp2cvbEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZNK6clHasp8getErrorEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK7clHasp28getErrorEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK6clHasp12isLowBatteryEv", reinterpret_cast<void *>(returnHaspSuccess));
    n2HookSymbol("_ZNK6clHasp11getSerialHiEv", reinterpret_cast<void *>(getN2DongleSerialHi));
    n2HookSymbol("_ZNK6clHasp11getSerialLoEv", reinterpret_cast<void *>(getN2DongleSerialLo));
    n2HookSymbol("_ZNK7clHasp211getSerialHiEv", reinterpret_cast<void *>(getN2Dongle2SerialHi));
    n2HookSymbol("_ZNK7clHasp211getSerialLoEv", reinterpret_cast<void *>(getN2Dongle2SerialLo));

    if (!isValidN2DongleId(getConfig()->namcoN2.dongleId))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId);
    if (!isValidN2DongleId(getConfig()->namcoN2.dongleId2))
        log_warn("Namco N2: [NamcoN2] DONGLE_ID_2 is not 12 decimal digits; using virtual ID %s.",
                 defaultN2DongleId2);
    return 0;
}

#endif
