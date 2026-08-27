#include "banapassport.hpp"
#include "nbgiCodec.hpp"
#include "../../common/cardControl.h"
#include "../../../elfLoader/guestTls.hpp"
#include "../../../log/log.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <windows.h>

namespace
{
/* The record holds the identity twice: raw IDm/PMm, and the ASCII hex the
 * titles read. Amusement IC keys on the IDm, so the two must agree. */
constexpr size_t IdmOffset = 0x1c;
constexpr size_t IdmSize = 8;
constexpr size_t PmmOffset = 0x24;
constexpr size_t PmmSize = 8;
constexpr size_t ChipIdOffset = 0x2c;
constexpr size_t AccessCodeOffset = 0x50;
constexpr size_t CardStringSize = 32;
/* Amusement IC access codes are twenty digits. */
constexpr size_t AccessCodeDigits = 20;
constexpr size_t NbgicHeaderOffset = 0x13f;

constexpr char DefaultChipId[] = "7F5C9744F111111143262C3300040610";
constexpr char DefaultAccessCode[] = "30764352518498790910";
constexpr uint32_t DefaultNbgicSerial = 0x0069e9fa;
constexpr uint16_t DefaultNbgicUnknown = 0x03f6;
constexpr uint8_t DefaultNbgicFlags = 0;
constexpr uint8_t DefaultNbgicKey = nbgi::Nbgic6KeyNumber;

/* A real dump; "NBGIC6" at 0x78 is the card-type marker. */
const std::array<uint8_t, BANAPASSPORT_RECORD_SIZE> DefaultCardRecord = {
    0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x92, 0x2e, 0x58, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0x5c, 0x97, 0x44, 0xf0, 0x88, 0x04, 0x00,
    0x43, 0x26, 0x2c, 0x33, 0x00, 0x04, 0x06, 0x10, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4e, 0x42, 0x47, 0x49, 0x43, 0x36, 0x00, 0x00, 0xfa, 0xe9, 0x69, 0x00,
    0xf6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::array<uint8_t, BANAPASSPORT_RECORD_SIZE> g_cardRecord = DefaultCardRecord;
std::array<uint8_t, nbgi::HeaderSize> g_nbgicHeader{};
std::atomic<bool> g_cardLoaded{false};

/* A wait-touch arriving with an empty reader stays pending. */
std::mutex g_slotMutex;
bool g_cardPresent = false;
BanapassportTouchCallback g_pendingTouch = nullptr;
int g_pendingTouchDevice = 0;
void *g_pendingTouchUser = nullptr;
std::string g_readerName = "Banapassport IC card";

constexpr char DefaultConfigFile[] = "banapassport.ini";
std::atomic<bool> g_configured{false};
int g_readerEnabled = 1;
int g_autoInsert = 1;
int g_diagnostics = 0;

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Chip id as the titles read it: uppercase hex of IDm followed by PMm. */
std::string chipIdFromBinary(const uint8_t *idm, const uint8_t *pmm)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(CardStringSize);
    for (size_t i = 0; i < IdmSize; ++i)
    {
        text.push_back(digits[idm[i] >> 4]);
        text.push_back(digits[idm[i] & 0x0f]);
    }
    for (size_t i = 0; i < PmmSize; ++i)
    {
        text.push_back(digits[pmm[i] >> 4]);
        text.push_back(digits[pmm[i] & 0x0f]);
    }
    return text;
}

/* The chip id is the authority - it is what the server agrees on - and the raw
 * IDm/PMm are derived from it. A physical reader inverts this. */
std::array<uint8_t, BANAPASSPORT_RECORD_SIZE> buildRecord(const std::string &chipId,
                                                          const std::string &accessCode)
{
    std::array<uint8_t, BANAPASSPORT_RECORD_SIZE> record = DefaultCardRecord;

    uint8_t identity[IdmSize + PmmSize] = {};
    bool decoded = chipId.size() == CardStringSize;
    for (size_t i = 0; decoded && i < sizeof(identity); ++i)
    {
        const int hi = hexDigit(chipId[i * 2]);
        const int lo = hexDigit(chipId[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            decoded = false;
        else
            identity[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    if (decoded)
    {
        std::memcpy(record.data() + IdmOffset, identity, IdmSize);
        std::memcpy(record.data() + PmmOffset, identity + IdmSize, PmmSize);
    }
    else
    {
        log_warn("Banapassport: ChipId '%s' is not 32 hex digits; leaving the "
                 "record's IDm untouched", chipId.c_str());
    }

    const std::string text = decoded
        ? chipIdFromBinary(record.data() + IdmOffset, record.data() + PmmOffset)
        : chipId;

    std::memset(record.data() + ChipIdOffset, 0, CardStringSize);
    std::memset(record.data() + AccessCodeOffset, 0, CardStringSize);
    std::memcpy(record.data() + ChipIdOffset, text.c_str(),
                text.size() < CardStringSize ? text.size() : CardStringSize);
    std::memcpy(record.data() + AccessCodeOffset, accessCode.c_str(),
                accessCode.size() < CardStringSize ? accessCode.size() : CardStringSize);

    if (accessCode.size() != AccessCodeDigits)
        log_warn("Banapassport: AccessCode '%s' is %u digits, not %u",
                 accessCode.c_str(), static_cast<unsigned>(accessCode.size()),
                 static_cast<unsigned>(AccessCodeDigits));
    return record;
}

/* Hand a card to whoever is waiting for one. Called with g_slotMutex held. */
void completeTouchLocked()
{
    if (!g_pendingTouch || !g_cardPresent)
        return;
    const BanapassportTouchCallback callback = g_pendingTouch;
    const int deviceId = g_pendingTouchDevice;
    void *userData = g_pendingTouchUser;
    g_pendingTouch = nullptr;
    GuestTls::EnterGuestCode();
    callback(deviceId, BANAPASSPORT_STATUS_OK, g_cardRecord.data(), userData);
    GuestTls::EnterHostCall();
}

std::string readCardValue(const char *file, const char *key, const char *fallback)
{
    char value[CardStringSize + 1] = {};
    GetPrivateProfileStringA("Card", key, fallback, value, sizeof(value), file);
    return value;
}

int readCardFlag(const char *file, const char *key, int fallback)
{
    char value[16] = {};
    GetPrivateProfileStringA("Reader", key, fallback ? "true" : "false", value,
                             sizeof(value), file);
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0) return 1;
    if (std::strcmp(value, "false") == 0 || std::strcmp(value, "0") == 0) return 0;
    return fallback;
}

uint32_t readCardUnsigned(const char *file, const char *key, uint32_t fallback)
{
    char fallbackText[32] = {};
    char value[32] = {};
    std::snprintf(fallbackText, sizeof(fallbackText), "0x%08x", fallback);
    GetPrivateProfileStringA("Card", key, fallbackText, value, sizeof(value), file);

    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    if (end == value || parsed > 0xfffffffful)
        return fallback;
    return static_cast<uint32_t>(parsed);
}

/* Written on first run so the numbers are editable, not buried in the loader. */
void writeDefaultConfigIfMissing(const char *path)
{
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        return;

    FILE *file = std::fopen(path, "w");
    if (!file)
    {
        log_warn("Banapassport: could not create %s; built-in defaults are in use", path);
        return;
    }
    std::fprintf(file,
        "; Banapassport / Amusement IC card.\n"
        ";\n"
        "; The reader and the card it holds are configured here rather than in\n"
        "; linuxloader.ini, because one reader is shared by every title that uses\n"
        "; Banapassport.\n"
        "\n"
        "[Reader]\n"
        "; false answers the reader's calls but never delivers a card.\n"
        "ENABLED = true\n"
        "; A card sits on the reader from start-up. Turn this off once a physical\n"
        "; reader is wired in, so that touching a card is what delivers it.\n"
        "AUTO_INSERT = true\n"
        "; Logs the chip id and access code actually loaded.\n"
        "DIAGNOSTICS = false\n"
        "\n"
        "[Card]\n"
        "; Chip id: FeliCa IDm followed by PMm, 32 hex digits.\n"
        "ChipId = %s\n"
        "; Legacy NBGIC values generate the 20-digit access code.\n"
        "NBGIC_SERIAL = 0x%08x\n"
        "NBGIC_KEY = %u\n"
        "NBGIC_UNKNOWN = 0x%04x\n"
        "NBGIC_FLAGS = 0x%02x\n"
        "AccessCode = %s\n",
        DefaultChipId, DefaultNbgicSerial, static_cast<unsigned>(DefaultNbgicKey),
        static_cast<unsigned>(DefaultNbgicUnknown), static_cast<unsigned>(DefaultNbgicFlags),
        DefaultAccessCode);
    std::fclose(file);
    log_info("Banapassport: created %s", path);
}

CardControlActionResult setInsertState(int active)
{
    if (!active)
        return CARD_CONTROL_HANDLED;
    banapassportPresent(nullptr, nullptr);
    return CARD_CONTROL_HANDLED_ONE_SHOT;
}

CardControlActionResult requestEject(void)
{
    banapassportRemove();
    return CARD_CONTROL_HANDLED_ONE_SHOT;
}

CardControlConnectionState connectionState(void)
{
    /* The emulated reader is always present; only the card comes and goes. */
    return CARD_CONTROL_CONNECTED;
}

const char *connectionText(void)
{
    std::lock_guard<std::mutex> guard(g_slotMutex);
    return g_cardPresent ? "card on reader" : "no card";
}

void logDiagnostics(void)
{
    std::lock_guard<std::mutex> guard(g_slotMutex);
    const char *chip = reinterpret_cast<const char *>(g_cardRecord.data() + ChipIdOffset);
    const char *access = reinterpret_cast<const char *>(g_cardRecord.data() + AccessCodeOffset);
    log_info("Banapassport: %s, chip=%.32s access=%.32s, touch %s",
             g_cardPresent ? "card on reader" : "no card", chip, access,
             g_pendingTouch ? "pending" : "idle");
}
} // namespace

extern "C" int banapassportBadDevice(int deviceId)
{
    return deviceId < 0 || deviceId >= BANAPASSPORT_MAX_DEVICES;
}

extern "C" void banapassportComplete(BanapassportCommandCallback callback, int deviceId,
                                     void *userData)
{
    if (!callback)
        return;
    GuestTls::EnterGuestCode();
    callback(deviceId, BANAPASSPORT_STATUS_OK, userData);
    GuestTls::EnterHostCall();
}

extern "C" int banapassportWaitTouch(int deviceId, BanapassportTouchCallback callback,
                                     void *userData, int autoInsert)
{
    if (banapassportBadDevice(deviceId))
        return BANAPASSPORT_BAD_DEVICE;
    if (!callback)
        return BANAPASSPORT_OK;

    std::lock_guard<std::mutex> guard(g_slotMutex);
    if (autoInsert)
        g_cardPresent = true;

    g_pendingTouch = callback;
    g_pendingTouchDevice = deviceId;
    g_pendingTouchUser = userData;
    completeTouchLocked();
    return BANAPASSPORT_OK;
}

extern "C" void banapassportPresent(const char *chipId, const char *accessCode)
{
    std::lock_guard<std::mutex> guard(g_slotMutex);
    if (chipId && accessCode && *chipId && *accessCode)
    {
        g_cardRecord = buildRecord(chipId, accessCode);
        g_cardLoaded.store(true, std::memory_order_release);
        log_info("Banapassport: card presented, chip=%s", chipId);
    }
    g_cardPresent = true;
    completeTouchLocked();
}

extern "C" void banapassportRemove(void)
{
    std::lock_guard<std::mutex> guard(g_slotMutex);
    g_cardPresent = false;
}

extern "C" const uint8_t *banapassportRecord(void)
{
    return g_cardRecord.data();
}

extern "C" int banapassportCardOnReader(void)
{
    std::lock_guard<std::mutex> guard(g_slotMutex);
    return g_cardPresent ? 1 : 0;
}

extern "C" int banapassportWriteNbgicHeader(void *buffer)
{
    if (!buffer)
        return 0;

    std::lock_guard<std::mutex> guard(g_slotMutex);
    if (!g_cardPresent)
        return 0;

    std::memcpy(static_cast<uint8_t *>(buffer) + NbgicHeaderOffset,
                g_nbgicHeader.data(), g_nbgicHeader.size());
    return 1;
}

extern "C" int banapassportReaderEnabled(void) { return g_readerEnabled; }
extern "C" int banapassportAutoInsert(void) { return g_autoInsert; }
extern "C" int banapassportDiagnostics(void) { return g_diagnostics; }

extern "C" void banapassportConfigure(const char *file)
{
    if (g_configured.exchange(true, std::memory_order_acq_rel))
        return;

    const char *path = (file && *file) ? file : DefaultConfigFile;
    writeDefaultConfigIfMissing(path);

    g_readerEnabled = readCardFlag(path, "ENABLED", 1);
    g_autoInsert = readCardFlag(path, "AUTO_INSERT", 1);
    g_diagnostics = readCardFlag(path, "DIAGNOSTICS", 0);

    const std::string chipId = readCardValue(path, "ChipId", DefaultChipId);
    const uint32_t serial = readCardUnsigned(path, "NBGIC_SERIAL", DefaultNbgicSerial);
    const uint32_t keyValue = readCardUnsigned(path, "NBGIC_KEY", DefaultNbgicKey);
    const uint32_t unknownValue = readCardUnsigned(path, "NBGIC_UNKNOWN", DefaultNbgicUnknown);
    const uint32_t flagsValue = readCardUnsigned(path, "NBGIC_FLAGS", DefaultNbgicFlags);
    const bool validSettings = keyValue < 8 && unknownValue <= 0xffff && flagsValue <= 0xff;
    const uint8_t keyNumber = validSettings ? static_cast<uint8_t>(keyValue) : DefaultNbgicKey;
    char generatedAccessCode[AccessCodeDigits + 1] = {};
    if (!validSettings || !nbgi::encodeHeader(keyNumber, serial, static_cast<uint16_t>(unknownValue),
                            static_cast<uint8_t>(flagsValue), g_nbgicHeader.data(),
                            g_nbgicHeader.size()) ||
        !nbgi::encodeAccessCode(keyNumber, serial, generatedAccessCode,
                                sizeof(generatedAccessCode)))
    {
        log_warn("Banapassport: invalid NBGIC settings; using built-in defaults");
        nbgi::encodeHeader(DefaultNbgicKey, DefaultNbgicSerial, DefaultNbgicUnknown,
                           DefaultNbgicFlags, g_nbgicHeader.data(), g_nbgicHeader.size());
        std::memcpy(generatedAccessCode, DefaultAccessCode, sizeof(generatedAccessCode));
    }

    const std::string accessCode = generatedAccessCode;
    {
        std::lock_guard<std::mutex> guard(g_slotMutex);
        g_cardRecord = buildRecord(chipId, accessCode);
    }

    log_info("Banapassport: %s (reader %s, auto-insert %s)", path,
             g_readerEnabled ? "on" : "off", g_autoInsert ? "on" : "off");
    if (g_diagnostics)
        log_info("Banapassport: chip=%s access=%s NBGIC key=%u serial=%08x",
                 chipId.c_str(), accessCode.c_str(), static_cast<unsigned>(keyNumber), serial);
    /* stdout is block-buffered once redirected, so these never reach a log file
     * on their own. */
    std::fflush(stdout);
}

extern "C" void banapassportRegisterCardControl(const char *name)
{
    if (name && *name)
        g_readerName = name;
    const CardControlBackend backend = {
        g_readerName.c_str(),
        setInsertState,
        requestEject,
        connectionState,
        connectionText,
        logDiagnostics
    };
    cardControlSetBackend(&backend);
}
