#include "es1Wmmt5Dongle.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <array>
#include <cstdint>
#include <cstring>

#include "../es1CompatLayer.h"
#include "../../../../config/config.h"
#include "../../../../log/log.h"

/* The HASP licence dongle WMN5r stops at E1910 without: a flat 0xD40 image
 * with the serial at 0xD00 and a checksum pair at the end. */
namespace
{

constexpr int DongleSize = 0xD40;
std::array<uint8_t, DongleSize> g_dongle{};
bool g_initialized = false;
constexpr uint32_t DongleHandle = 1;

/* Drive and terminal cabinets carry different serials. */
constexpr char DriveSerial[] = "267620542069";
constexpr char TerminalSerial[] = "267621542069";

void initializeDongle()
{
    if (g_initialized)
        return;
    g_initialized = true;

    g_dongle.fill(0);

    /* Two identical licence records; the title refuses a dongle whose records disagree. */
    g_dongle[0x00] = 0x01;
    for (int record = 0; record < 2; ++record)
    {
        const int base = 0x13 + record * 0x10;
        g_dongle[base] = 0x01;
        g_dongle[base + 0x04] = 0x0a;
        g_dongle[base + 0x08] = 0x04;
        g_dongle[base + 0x09] = 0x3b;
        g_dongle[base + 0x0a] = 0x6b;
        g_dongle[base + 0x0b] = 0x40;
        g_dongle[base + 0x0c] = 0x87;
    }

    const char *serial = getConfig()->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL
                             ? TerminalSerial
                             : DriveSerial;
    std::memcpy(g_dongle.data() + 0xD00, serial, 12);

    uint8_t checksum = 0;
    for (int i = 0; i < 62; ++i)
        checksum = static_cast<uint8_t>(checksum + g_dongle[0xD00 + i]);
    g_dongle[0xD3E] = checksum;
    g_dongle[0xD3F] = static_cast<uint8_t>(checksum ^ 0xFF);

    log_info("System ES1 WMMT5: virtual dongle ready (S/N %.6s-%.6s)", serial, serial + 6);
}

int wmmt5HaspLogin(int, int, uint32_t *handle)
{
    initializeDongle();
    if (handle)
        *handle = DongleHandle;
    return 0;
}

int wmmt5HaspSuccess(void) { return 0; }

int wmmt5HaspGetSize(int, int, int *size)
{
    if (size)
        *size = DongleSize;
    return 0;
}

int wmmt5HaspRead(uint32_t, uint32_t, int offset, int length, uint8_t *buffer)
{
    initializeDongle();
    if (!buffer || offset < 0 || length < 0 || offset > DongleSize ||
        length > DongleSize - offset)
        return 1;
    std::memcpy(buffer, g_dongle.data() + offset, static_cast<size_t>(length));
    return 0;
}

int wmmt5HaspWrite(uint32_t, uint32_t, int offset, int length, const uint8_t *buffer)
{
    initializeDongle();
    if (!buffer || offset < 0 || length < 0 || offset > DongleSize ||
        length > DongleSize - offset)
        return 1;
    std::memcpy(g_dongle.data() + offset, buffer, static_cast<size_t>(length));
    return 0;
}

constexpr uintptr_t LoginAddress = 0x0a982740;
constexpr uintptr_t LogoutAddress = 0x0a9827e0;
constexpr uintptr_t EncryptAddress = 0x0a9828cc;
constexpr uintptr_t DecryptAddress = 0x0a9829b8;
constexpr uintptr_t GetSizeAddress = 0x0a9836d0;
constexpr uintptr_t ReadAddress = 0x0a983538;
constexpr uintptr_t WriteAddress = 0x0a983604;

/* Prologues guard the build, not the identity, so repeats are expected. */
constexpr uint8_t SessionSignature[] = {0x83, 0xec, 0x14, 0x56, 0x53, 0x8b, 0x74, 0x24, 0x20};
constexpr uint8_t CipherSignature[] = {0x83, 0xec, 0x14, 0x56, 0x53, 0x8b, 0x74, 0x24, 0x28};
constexpr uint8_t StorageSignature[] = {0x83, 0xec, 0x18, 0x53, 0x8b, 0x5c, 0x24, 0x20};

} // namespace

void es1Wmmt5InstallDongleHooks(void)
{
    if (!getConfig()->namcoES1.dongleEnabled)
    {
        log_info("System ES1 WMMT5: dongle emulation disabled by configuration");
        return;
    }

    const Es1HookSpec hooks[] = {
        {LoginAddress, reinterpret_cast<void *>(wmmt5HaspLogin), "hasp_login", nullptr,
         SessionSignature, sizeof(SessionSignature)},
        {LogoutAddress, reinterpret_cast<void *>(wmmt5HaspSuccess), "hasp_logout", nullptr,
         SessionSignature, sizeof(SessionSignature)},
        {EncryptAddress, reinterpret_cast<void *>(wmmt5HaspSuccess), "hasp_encrypt", nullptr,
         CipherSignature, sizeof(CipherSignature)},
        {DecryptAddress, reinterpret_cast<void *>(wmmt5HaspSuccess), "hasp_decrypt", nullptr,
         CipherSignature, sizeof(CipherSignature)},
        {GetSizeAddress, reinterpret_cast<void *>(wmmt5HaspGetSize), "hasp_get_size", nullptr,
         StorageSignature, sizeof(StorageSignature)},
        {ReadAddress, reinterpret_cast<void *>(wmmt5HaspRead), "hasp_read", nullptr,
         StorageSignature, sizeof(StorageSignature)},
        {WriteAddress, reinterpret_cast<void *>(wmmt5HaspWrite), "hasp_write", nullptr,
         StorageSignature, sizeof(StorageSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 dongle");
}

#endif
