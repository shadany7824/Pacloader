#include "es1MaximumHeat3D.hpp"
#include "../es1TestModeCompat.h"

#include "../../../../elfLoader/symbolResolver.hpp"
#include "../../../../log/log.h"
#include "../../../../../minhook/include/MinHook.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::string readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

bool has(const std::string &contents, const char *needle)
{
    return contents.find(needle) != std::string::npos;
}

constexpr int DongleSize = 0xD40;
std::array<unsigned char, DongleSize> g_dongle{};
uint32_t g_dongleHandle = 0x45533101;
bool g_dongleInitialized = false;

using VolumeSetter = void (*)(int);
VolumeSetter g_setBgmVolumeOriginal = nullptr;
VolumeSetter g_setSeVolumeOriginal = nullptr;
VolumeSetter g_setEngineVolumeOriginal = nullptr;
VolumeSetter g_setVoiceVolumeOriginal = nullptr;
using BoolVolumeSetter = void (*)(int, bool);
BoolVolumeSetter g_setMasterVolumeOriginal = nullptr;
BoolVolumeSetter g_setAttractVolumeOriginal = nullptr;

/* The cabinet exposes a 16-step volume table.  Keep the game's own table access
 * in range when the host audio device reports no hardware mixer, as the
 * physical cabinet does. */
int clampVolume(const char *what, int volume)
{
    const int safe = std::clamp(volume, 0, 15);
    if (safe != volume)
        log_warn("System ES1 audio: clamped %s volume index %d to %d", what, volume, safe);
    return safe;
}

void setBgmVolume(int volume)
{
    if (g_setBgmVolumeOriginal)
        g_setBgmVolumeOriginal(clampVolume("BGM", volume));
}

void setSeVolume(int volume)
{
    if (g_setSeVolumeOriginal)
        g_setSeVolumeOriginal(clampVolume("SE", volume));
}

void setEngineVolume(int volume)
{
    if (g_setEngineVolumeOriginal)
        g_setEngineVolumeOriginal(clampVolume("engine", volume));
}

void setVoiceVolume(int volume)
{
    if (g_setVoiceVolumeOriginal)
        g_setVoiceVolumeOriginal(clampVolume("voice", volume));
}

void setMasterVolume(int volume, bool enabled)
{
    if (g_setMasterVolumeOriginal)
        g_setMasterVolumeOriginal(clampVolume("master", volume), enabled);
}

void setAttractVolume(int volume, bool enabled)
{
    if (g_setAttractVolumeOriginal)
        g_setAttractVolumeOriginal(clampVolume("attract", volume), enabled);
}

void initializeDongle()
{
    if (g_dongleInitialized)
        return;

    g_dongle.fill(0);
    static constexpr char serial[] = "880700000001";
    std::memcpy(g_dongle.data() + 0xD00, serial, 12);
    unsigned char checksum = 0;
    for (int i = 0; i < 0x3E; ++i)
        checksum = static_cast<unsigned char>(checksum + g_dongle[0xD00 + i]);
    g_dongle[0xD3E] = checksum;
    g_dongle[0xD3F] = static_cast<unsigned char>(checksum ^ 0xFF);
    g_dongleInitialized = true;
    log_info("System ES1: virtual dongle initialized (S/N %.6s-%.6s)", serial, serial + 6);
}

int haspLogin(int, int, uint32_t *handle)
{
    initializeDongle();
    if (handle)
        *handle = g_dongleHandle;
    return 0;
}

int haspGetSize(int, int, int *size)
{
    if (size)
        *size = DongleSize;
    return 0;
}

int haspRead(int, int, int offset, int length, unsigned char *buffer)
{
    initializeDongle();
    if (!buffer || offset < 0 || length < 0 || offset > DongleSize ||
        length > DongleSize - offset)
        return 1;
    std::memcpy(buffer, g_dongle.data() + offset, static_cast<size_t>(length));
    return 0;
}

int haspWrite(int, int, int offset, int length, const unsigned char *buffer)
{
    initializeDongle();
    if (!buffer || offset < 0 || length < 0 || offset > DongleSize ||
        length > DongleSize - offset)
        return 1;
    std::memcpy(g_dongle.data() + offset, buffer, static_cast<size_t>(length));
    return 0;
}

int haspSuccess()
{
    return 0;
}

int haspSerial()
{
    return 0x45533101;
}

/*
 * The title bootstraps through a legacy clSystemN2 object even though it is an
 * ES1 game, and the missing N2 JVIO board leaves its error byte set, which
 * becomes the E30 screen.  The real devices come from the virtual-device layer.
 */
bool systemIsError(void *)
{
    static bool logged = false;
    if (!logged)
    {
        log_info("System ES1: ignored legacy clSystemN2 bootstrap error");
        logged = true;
    }
    return false;
}

bool systemIsErrorConnectionCheck(void *)
{
    return false;
}

void *resolveHookTarget(const char *name)
{
    std::string module;
    void *target = SymbolResolver::GetInstance().ResolveSymbol(name, &module);
    return module == "UNRESOLVED_STUB" ? nullptr : target;
}

int installHook(const char *name, void *replacement)
{
    void *target = resolveHookTarget(name);
    if (!target)
    {
        log_warn("System ES1: optional hook target not found: %s", name);
        return 0;
    }

    const MH_STATUS status = MH_CreateHook(target, replacement, nullptr);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        log_error("System ES1: failed to hook %s (MinHook status %d)", name,
                  static_cast<int>(status));
        return 0;
    }
    log_info("System ES1: hooked %s at %p", name, target);
    return 1;
}

int installVolumeHook(const char *name, void *replacement, void **original)
{
    void *target = resolveHookTarget(name);
    if (!target)
    {
        log_warn("System ES1 audio: optional volume target not found: %s", name);
        return 0;
    }

    const MH_STATUS status = MH_CreateHook(target, replacement, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        log_warn("System ES1 audio: failed to hook %s (MinHook status %d)", name,
                 static_cast<int>(status));
        return 0;
    }
    log_info("System ES1 audio: clamped volume setter %s at %p", name, target);
    return 1;
}
} // namespace

extern "C" int es1MaximumHeat3DDetect(const char *elfPath)
{
    const std::filesystem::path elf(elfPath);
    const std::filesystem::path root = elf.parent_path().parent_path().parent_path();
    const std::string info = readFile(root / "info");
    const std::string csv = readFile(root / "data" / "csv" / "config.csv");

    /*
     * There is no N2 gRomInfo marker to go by.  The package metadata and the
     * JAMMA/camera/display configuration are stable across the known dump, so
     * require that evidence rather than trusting the directory name.
     */
    const bool packageName = has(info, "US DRIVE") || has(info, "Maximum Heat 3D");
    const bool es1Config = has(csv, "USE_JAMMA_DEVICE") &&
                           has(csv, "USE_CAMERA_DEVICE") &&
                           has(csv, "VIDEO_XSIZE=1360") &&
                           has(csv, "VIDEO_YSIZE=768");
    return packageName || es1Config ? 1 : 0;
}

extern "C" int es1MaximumHeat3DInstallHooks(void)
{
    if (es1InstallTestModeCompatHook() != 0)
        return -1;

    int installed = 0;
    installed += installHook("hasp_login", reinterpret_cast<void *>(haspLogin));
    installed += installHook("hasp_logout", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_get_size", reinterpret_cast<void *>(haspGetSize));
    installed += installHook("hasp_read", reinterpret_cast<void *>(haspRead));
    installed += installHook("hasp_write", reinterpret_cast<void *>(haspWrite));
    installed += installHook("hasp_cleanup", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_encrypt", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_decrypt", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_free", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_get_rtc", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("hasp_get_sessioninfo", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("_ZNK6clHASP7IsErrorEv", reinterpret_cast<void *>(haspSuccess));
    installed += installHook("_ZNK6clHASP11GetSerialNoEv", reinterpret_cast<void *>(haspSerial));
    installed += installHook("_ZN10clSystemN27isErrorEv",
                             reinterpret_cast<void *>(systemIsError));
    installed += installHook("_ZN10clSystemN222isErrorConnectionCheckEv",
                             reinterpret_cast<void *>(systemIsErrorConnectionCheck));
    installed += installVolumeHook("_ZN7nsAudio15SNDSetBgmVolumeEi",
                                   reinterpret_cast<void *>(setBgmVolume),
                                   reinterpret_cast<void **>(&g_setBgmVolumeOriginal));
    installed += installVolumeHook("_ZN7nsAudio14SNDSetSeVolumeEi",
                                   reinterpret_cast<void *>(setSeVolume),
                                   reinterpret_cast<void **>(&g_setSeVolumeOriginal));
    installed += installVolumeHook("_ZN7nsAudio18SNDSetEngineVolumeEi",
                                   reinterpret_cast<void *>(setEngineVolume),
                                   reinterpret_cast<void **>(&g_setEngineVolumeOriginal));
    installed += installVolumeHook("_ZN7nsAudio17SNDSetVoiceVolumeEi",
                                   reinterpret_cast<void *>(setVoiceVolume),
                                   reinterpret_cast<void **>(&g_setVoiceVolumeOriginal));
    installed += installVolumeHook("_ZN7nsAudio18SNDSetMasterVolumeEib",
                                   reinterpret_cast<void *>(setMasterVolume),
                                   reinterpret_cast<void **>(&g_setMasterVolumeOriginal));
    installed += installVolumeHook("_ZN7nsAudio19SNDSetAttractVolumeEib",
                                   reinterpret_cast<void *>(setAttractVolume),
                                   reinterpret_cast<void **>(&g_setAttractVolumeOriginal));
    log_info("System ES1: installed %d independent HASP/legacy compatibility hooks", installed);
    return installed > 0 ? 0 : -1;
}
