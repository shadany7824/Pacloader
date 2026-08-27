#include "es1Kizuna.hpp"
#include "../es1Title.h"

#include "../../../../elfLoader/symbolResolver.hpp"
#include "../../../../elfLoader/virtualDeviceRegistry.hpp"
#include "../../../../log/log.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace
{
/* The cabinet's own launcher picks the executable and its arguments from two
 * probe binaries it runs first - n2jvio_bootselect decides terminal vs station,
 * station_projectorselect decides which projector is fitted:
 *
 *   n_gun_station_rel_opt_es1 fullscreen fullhd   FD630, 1920x1080
 *   n_gun_station_rel_opt_es1 fullscreen xga      T250,  1024x768
 *   n_gun_terminal_rel_opt_es1 fullscreen
 *
 * The loader runs one ELF, so the probes never happen and the arguments come
 * from the game path instead: "n_gun_station_rel_opt_es1 fullscreen xga".
 * Without them the title has no display mode and stops early. */
constexpr char StationElf[] = "n_gun_station_rel_opt_es1";
constexpr char DataDirectory[] = "data_senjyo_no_kizuna_revision3_nbgi";

/* /dev/ttyS2 is the JVS board and already has an owner.  The other three carry
 * the projector, the card reader and the POD's voice units; which is which is
 * not established yet, so all three are claimed and traced together.  Leaving
 * them unclaimed is what produced E[24-07]: the projector's step sequence
 * (KT_PROJCTRL_*_STEPSEQ_BOOTTEST_*) could not even open its port, ran out of
 * retries and took its REBOOT branch, which is why main() returned 100. */
constexpr int FirstDescriptor = 0x4f10;

struct Port
{
    const char *path;
    bool used;
    int fd;
    std::deque<unsigned char> pending;
};

std::mutex portsMutex;
std::array<Port, 3> ports{{{"/dev/ttyS0", false, -1, {}},
                           {"/dev/ttyS1", false, -1, {}},
                           {"/dev/ttyS3", false, -1, {}}}};

Port *findPort(int fd)
{
    for (Port &port : ports)
        if (port.used && port.fd == fd)
            return &port;
    return nullptr;
}

bool claims(const char *path)
{
    if (!path || !es1TitleIs("KIZUNA3"))
        return false;
    for (const Port &port : ports)
        if (std::strcmp(path, port.path) == 0)
            return true;
    return false;
}

int openDevice(const char *path, int flags)
{
    (void)flags;
    std::lock_guard<std::mutex> lock(portsMutex);
    for (Port &port : ports)
    {
        if (std::strcmp(path, port.path) != 0)
            continue;
        if (!port.used)
        {
            port.used = true;
            port.fd = FirstDescriptor + static_cast<int>(&port - ports.data());
            port.pending.clear();
        }
        log_info("Kizuna: opened %s as fd %d", path, port.fd);
        return port.fd;
    }
    errno = ENOENT;
    return -1;
}

int owns(int fd)
{
    std::lock_guard<std::mutex> lock(portsMutex);
    return findPort(fd) ? 1 : 0;
}

int available(int fd)
{
    std::lock_guard<std::mutex> lock(portsMutex);
    Port *port = findPort(fd);
    return port ? static_cast<int>(port->pending.size()) : 0;
}

int readDevice(int fd, void *buffer, size_t count)
{
    std::lock_guard<std::mutex> lock(portsMutex);
    Port *port = findPort(fd);
    if (!port || !buffer || count == 0)
        return 0;

    size_t copied = 0;
    unsigned char *out = static_cast<unsigned char *>(buffer);
    while (copied < count && !port->pending.empty())
    {
        out[copied++] = port->pending.front();
        port->pending.pop_front();
    }
    log_info("Kizuna serial %s -> asked %u, gave %u, %u still queued", port->path,
             static_cast<unsigned>(count), static_cast<unsigned>(copied),
             static_cast<unsigned>(port->pending.size()));
    return static_cast<int>(copied);
}

/* The projector speaks a framed protocol.  The title sends
 * STX + three command letters + optional parameter + ETX; the reply is
 * ACK + CR + payload, which is why the two directions are framed differently
 * and why the state machine has separate RECVACK, RECVCR and RECVDATA steps -
 * it reads the reply one byte at a time and abandons the exchange the moment a
 * byte fails its check.
 *
 * The executable carries the command table as 80-byte rows of
 * {sendLength, replyLength, letters, id, name}; the reply payload is
 * replyLength - 2 bytes, the two being the ACK and the CR.  A 5-byte command is
 * answered in 3, the 9-byte SSS in 6.  The table's name field sits one row
 * ahead of the letters it describes, which is how "SSS" resolves to
 * SPECIAL_SERVICE_STATUS rather than the RESET_LAMP_TIME stored beside it. */
constexpr unsigned char Ack = 0x06;
constexpr unsigned char Cr = 0x0d;
constexpr unsigned char Stx = 0x02;

struct ProjectorCommand
{
    const char *letters;
    size_t replyLength;
};

constexpr ProjectorCommand ProjectorCommands[] = {
    {"SSS", 6}, /* SPECIAL_SERVICE_STATUS, takes a 4-byte selector */
    {"ERL", 21}, /* READ_TOTAL_USE_LIST */
    {"LTS", 3},  /* SET_LAMP_TIME */
    {"LTR", 3},  /* RESET_LAMP_TIME */
    {"FSV", 3},  /* SAVE_FACTORY */
};

size_t projectorReplyLength(const unsigned char *frame, size_t count)
{
    if (count < 5 || frame[0] != Stx)
        return 0;
    for (const ProjectorCommand &entry : ProjectorCommands)
        if (std::memcmp(frame + 1, entry.letters, 3) == 0)
            return entry.replyLength;
    /* Everything else in the table is a 5-byte command answered in 3. */
    return 3;
}

void traceFrame(const char *path, const unsigned char *data, size_t count)
{
    std::string hex;
    std::string text;
    for (size_t i = 0; i < count && i < 64; ++i)
    {
        char byte[4];
        std::snprintf(byte, sizeof(byte), "%02x ", data[i]);
        hex += byte;
        text += (data[i] >= 32 && data[i] < 127) ? static_cast<char>(data[i]) : '.';
    }
    log_info("Kizuna serial %s <- %u bytes: %s| %s", path, static_cast<unsigned>(count),
             hex.c_str(), text.c_str());
}


/* The card reader frames the other way round: a leading 0xFA, the command, a
 * payload length and a checksum that is the low byte of everything before it
 * summed.  Observed at boot, once a second: "fa 01 00 fb" and "fa 0d 00 07".
 *
 * What it wants back is not established yet, so the reply is built from
 * LL_KIZUNA_CARD_REPLY when that is set and otherwise echoes the request.  The
 * useful signal is how many bytes the title reads before it gives up and
 * repeats itself - that is how the projector's framing was pinned down. */
constexpr unsigned char CardSync = 0xfa;

unsigned char cardChecksum(const unsigned char *bytes, size_t count)
{
    unsigned int sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += bytes[i];
    return static_cast<unsigned char>(sum & 0xff);
}

std::vector<unsigned char> parseHexBytes(const char *spec)
{
    std::vector<unsigned char> bytes;
    unsigned value = 0;
    const char *cursor = spec;
    while (cursor && *cursor && std::sscanf(cursor, "%2x", &value) == 1)
    {
        bytes.push_back(static_cast<unsigned char>(value));
        while (*cursor && *cursor != ' ')
            ++cursor;
        while (*cursor == ' ')
            ++cursor;
    }
    return bytes;
}

void queueCardReply(Port &port, const unsigned char *request, size_t count)
{
    static bool overrideRead = false;
    static std::vector<unsigned char> overrideBytes;
    if (!overrideRead)
    {
        overrideRead = true;
        if (const char *spec = std::getenv("LL_KIZUNA_CARD_REPLY"))
        {
            overrideBytes = parseHexBytes(spec);
            log_info("Kizuna card R/W: reply overridden with %u bytes",
                     static_cast<unsigned>(overrideBytes.size()));
        }
    }

    if (!overrideBytes.empty())
    {
        port.pending.insert(port.pending.end(), overrideBytes.begin(), overrideBytes.end());
        return;
    }

    if (count < 4 || request[0] != CardSync)
        return;

    unsigned char frame[4] = {CardSync, request[1], 0x00, 0x00};
    frame[3] = cardChecksum(frame, 3);
    port.pending.insert(port.pending.end(), frame, frame + 4);
    log_info("Kizuna card R/W: echoed command 0x%02x", request[1]);
}

int writeDevice(int fd, const void *buffer, size_t count)
{
    std::lock_guard<std::mutex> lock(portsMutex);
    Port *port = findPort(fd);
    if (!port || !buffer)
        return static_cast<int>(count);

    const unsigned char *data = static_cast<const unsigned char *>(buffer);
    traceFrame(port->path, data, count);

    if (std::strcmp(port->path, "/dev/ttyS0") == 0)
    {
        const size_t replyLength = projectorReplyLength(data, count);
        if (replyLength >= 2)
        {
            /* The framing of the reply is still being pinned down, so an
             * override is read once from LL_KIZUNA_PROJ_REPLY as hex bytes -
             * "06 0d 00 00 00 00" and so on - which lets a candidate be tried
             * without a rebuild. */
            static bool overrideRead = false;
            static std::vector<unsigned char> overrideBytes;
            if (!overrideRead)
            {
                overrideRead = true;
                if (const char *spec = std::getenv("LL_KIZUNA_PROJ_REPLY"))
                {
                    unsigned value = 0;
                    const char *cursor = spec;
                    while (std::sscanf(cursor, "%2x", &value) == 1)
                    {
                        overrideBytes.push_back(static_cast<unsigned char>(value));
                        while (*cursor && *cursor != ' ')
                            ++cursor;
                        while (*cursor == ' ')
                            ++cursor;
                        if (!*cursor)
                            break;
                    }
                    log_info("Kizuna projector: reply overridden with %u bytes",
                             static_cast<unsigned>(overrideBytes.size()));
                }
            }

            if (!overrideBytes.empty())
            {
                port->pending.insert(port->pending.end(), overrideBytes.begin(),
                                     overrideBytes.end());
            }
            else
            {
                /* A zero payload reads as "no fault" for the status queries and
                 * as a plain acknowledgement for the setting commands. */
                port->pending.push_back(Ack);
                port->pending.push_back(Cr);
                for (size_t i = 0; i + 2 < replyLength; ++i)
                    port->pending.push_back(0x00);
            }
            log_info("Kizuna projector: answered %.3s, %u bytes queued",
                     reinterpret_cast<const char *>(data + 1),
                     static_cast<unsigned>(port->pending.size()));
        }
    }

    else if (std::strcmp(port->path, "/dev/ttyS1") == 0)
    {
        queueCardReply(*port, data, count);
    }

    return static_cast<int>(count);
}

int closeDevice(int fd)
{
    std::lock_guard<std::mutex> lock(portsMutex);
    Port *port = findPort(fd);
    if (!port)
        return -1;
    port->used = false;
    port->fd = -1;
    port->pending.clear();
    return 0;
}

/* Line settings and modem-status queries all succeed; refusing them would make
 * the caller give up before it ever sends a command. */
int ioctlDevice(int fd, unsigned long request, void *argument)
{
    (void)argument;
    std::lock_guard<std::mutex> lock(portsMutex);
    Port *port = findPort(fd);
    if (!port)
        return -1;
    log_debug("Kizuna serial %s ioctl 0x%lx", port->path, request);
    return 0;
}


/* The cabinet keeps its settings in a key/value store on the /dev/crypt/registry
 * volume, reached through libarcaderegistry.  The real library loads fine here -
 * it only wants nineteen libc symbols - but the block device behind it does not
 * exist, so arcade_registry_load() fails and every lookup is skipped.  That is
 * what left the projector check enabled: the title reads
 * GKE_SEAL_DEBUG_NOCHECK_PROJECTOR and, when it comes back non-NULL, skips
 * talking to the projector entirely.
 *
 * Rather than reproduce the on-disk format, the six entry points the title
 * imports are answered here and backed by a plain text file beside the game, so
 * the settings are also editable by hand.  A registered VTable entry wins over
 * the symbol the real library exports.
 */
constexpr char RegistryFileName[] = "arcade-registry.ini";

std::mutex registryMutex;
std::map<std::string, std::string> registryValues;
bool registryLoaded = false;

void loadRegistryLocked()
{
    if (registryLoaded)
        return;
    registryLoaded = true;

    std::ifstream file(RegistryFileName);
    if (file)
    {
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            const size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;
            std::string key = line.substr(0, equals);
            std::string value = line.substr(equals + 1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                value.pop_back();
            registryValues[key] = value;
        }
        log_info("Kizuna registry: read %u keys from %s",
                 static_cast<unsigned>(registryValues.size()), RegistryFileName);
    }

    /* No projector is attached to an emulated cabinet, so the title's own
     * bypass is on unless the file says otherwise.  The value names the model
     * the launcher would have detected, which follows the display mode. */
    if (registryValues.find("GKE_SEAL_DEBUG_NOCHECK_PROJECTOR") == registryValues.end())
    {
        const Es1Title *title = es1CurrentTitle();
        const bool fullHd = title && title->width >= 1920;
        registryValues["GKE_SEAL_DEBUG_NOCHECK_PROJECTOR"] = fullHd ? "FD630" : "T250";
        log_info("Kizuna registry: defaulting GKE_SEAL_DEBUG_NOCHECK_PROJECTOR to %s",
                 registryValues["GKE_SEAL_DEBUG_NOCHECK_PROJECTOR"].c_str());
    }
}

void saveRegistryLocked()
{
    std::ofstream file(RegistryFileName, std::ios::trunc);
    if (!file)
    {
        log_warn("Kizuna registry: cannot write %s", RegistryFileName);
        return;
    }
    file << "# Namco arcade registry, as libarcaderegistry would have stored it.\n";
    for (const auto &entry : registryValues)
        file << entry.first << '=' << entry.second << '\n';
}

/* The handle is opaque to the title; it only ever passes it back. */
struct RegistryHandle
{
    unsigned int magic;
};
constexpr unsigned int RegistryMagic = 0x4b5a4e41;

void *bridgeRegistryNew(void)
{
    RegistryHandle *handle = new RegistryHandle{RegistryMagic};
    return handle;
}

int bridgeRegistryLoad(void *handle)
{
    if (!handle || static_cast<RegistryHandle *>(handle)->magic != RegistryMagic)
        return -1;
    std::lock_guard<std::mutex> lock(registryMutex);
    loadRegistryLocked();
    return 0;
}

/* The caller copies the result immediately, but map values keep a stable
 * address anyway, so returning one directly is safe. */
const char *bridgeRegistryRead(void *handle, const char *key)
{
    if (!handle || !key)
        return nullptr;
    std::lock_guard<std::mutex> lock(registryMutex);
    loadRegistryLocked();
    auto found = registryValues.find(key);
    if (found == registryValues.end())
    {
        log_debug("Kizuna registry: %s is not set", key);
        return nullptr;
    }
    log_info("Kizuna registry: %s = %s", key, found->second.c_str());
    return found->second.c_str();
}

int bridgeRegistryWrite(void *handle, const char *key, const char *value)
{
    if (!handle || !key)
        return -1;
    std::lock_guard<std::mutex> lock(registryMutex);
    loadRegistryLocked();
    registryValues[key] = value ? value : "";
    log_info("Kizuna registry: %s <- %s", key, value ? value : "");
    return 0;
}

int bridgeRegistryCommit(void *handle)
{
    if (!handle)
        return -1;
    std::lock_guard<std::mutex> lock(registryMutex);
    saveRegistryLocked();
    return 0;
}

void bridgeRegistryFree(void *handle)
{
    delete static_cast<RegistryHandle *>(handle);
}

const VirtualDeviceRegistry::Device serialPorts{
    "kizuna-serial",
    claims,
    openDevice,
    owns,
    available,
    readDevice,
    writeDevice,
    closeDevice,
    ioctlDevice,
    nullptr};
} // namespace

extern "C" int es1KizunaDetect(const char *elfPath)
{
    const std::filesystem::path elf(elfPath);
    const std::filesystem::path gameDir = elf.parent_path();

    /* The data directory carries the revision in its name and sits beside the
     * executable, which is enough to tell this package from any other ES1 one. */
    return elf.filename() == StationElf && std::filesystem::exists(gameDir / DataDirectory)
               ? 1
               : 0;
}

/* Called from es1PrepareLoad, before relocations: a VTable entry only wins over
 * libarcaderegistry's own export if it is registered before the symbol is
 * bound, and binding happens while the ELF is being loaded. */
extern "C" int es1KizunaPrepareLoad(void)
{
    if (!es1TitleIs("KIZUNA3"))
        return 0;

    MAP("arcade_registry_new", bridgeRegistryNew);
    MAP("arcade_registry_load", bridgeRegistryLoad);
    MAP("arcade_registry_read", bridgeRegistryRead);
    MAP("arcade_registry_write", bridgeRegistryWrite);
    MAP("arcade_registry_commit", bridgeRegistryCommit);
    MAP("arcade_registry_free", bridgeRegistryFree);
    log_info("Kizuna: serving the arcade registry from %s", RegistryFileName);
    return 0;
}

extern "C" int es1KizunaInstallHooks(void)
{
    VirtualDeviceRegistry::registerDevice(serialPorts);
    log_info("Kizuna: claiming /dev/ttyS0, /dev/ttyS1 and /dev/ttyS3 (ttyS2 is JVS)");

    /* The cabinet expects these to exist before it will store anything; a
     * missing save directory is why the title reports E[19-12], its "test mode
     * settings have been reset" message. */
    static const char *const storage[] = {
        "live", "live/disk", "live/disk/save_nosafe", "live/disk/save_nosafe/save_nosafe",
        "live/disk/maint", "live/disk/maint/seal_debug", "live/disk/maint/seal_debug/relay",
        "live/disk/maint/update", "live/disk/data", "live/disk/arcade0", "live/disk/arcade1",
        "live/image", "live/image/arcade"};
    std::error_code ignored;
    for (const char *directory : storage)
        std::filesystem::create_directory(directory, ignored);

    return 0;
}
