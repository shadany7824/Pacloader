#include "n2Wmmt3.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include "../n2.h"
#include "../n2Audio.h"
#include "../n2Graphics.hpp"
#include "../n2Hasp.h"
#include "../n2Hook.h"
#include "../n2SteeringIo.h"
#include "../n2Title.h"
#include "../../../common/cardControl.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../../../config/config.h"
#include "../../../../elfLoader/symbolResolver.hpp"
#include "../../../../log/log.h"

namespace
{
#pragma pack(push, 1)
struct RomInfo
{
    char name[32];
    char region[32];
    char releaseType[32];
    char date[32];
    char time[32];
    int32_t revision;
    char revisionName[32];
};
#pragma pack(pop)

bool isPrintableString(const char *value, size_t capacity)
{
    size_t length = 0;
    for (; length < capacity && value[length] != '\0'; ++length)
    {
        if (!std::isprint(static_cast<unsigned char>(value[length])))
            return false;
    }
    return length > 0 && length < capacity;
}
int returnSuccess()
{
    return 1;
}
/*
 * emGCPResult value 3 is the cabinet's
 * "E51 リーダライターの接続を確認してください", i.e. the reader did not answer.
 */
constexpr int gcpResultReaderDisconnected = 3;

/*
 * clCardDeviceGameService keeps its public result/status block at +0x2c and a
 * non-null active process at +0x34.  Completing a request means filling that
 * block in and clearing the process so the caller stops waiting.
 */
int completeCardDeviceRequest(uint8_t *service, int result)
{
    if (!service)
        return 0;

    uint8_t *status = *reinterpret_cast<uint8_t **>(service + 0x2C);
    if (status)
    {
        *reinterpret_cast<int *>(status) = result;
        *reinterpret_cast<int *>(status + 0x08) = 0; // no card inserted
        *(status + 0x0C) = 0;                        // dispenser available
    }
    *reinterpret_cast<void **>(service + 0x34) = nullptr;
    return 1;
}

using CardRequest = int (*)(uint8_t *service);
using CardRequestFlag = int (*)(uint8_t *service, int flag);

CardRequest originalRequestGetStatus = nullptr;
CardRequestFlag originalRequestInit = nullptr;
CardRequest originalRequestCheckDispenser = nullptr;

int getStatusCardDevice(uint8_t *service)
{
    if (cardControlIsConnected() && originalRequestGetStatus)
        return originalRequestGetStatus(service);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}

int initCardDevice(uint8_t *service, int flag)
{
    if (cardControlIsConnected() && originalRequestInit)
        return originalRequestInit(service, flag);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}

int checkDispenserCardDevice(uint8_t *service)
{
    if (cardControlIsConnected() && originalRequestCheckDispenser)
        return originalRequestCheckDispenser(service);
    return completeCardDeviceRequest(service, gcpResultReaderDisconnected);
}
bool (*originalSystemIsError)(void) = nullptr;
bool systemErrorReported = false;

bool traceSystemIsError(void)
{
    const bool result = originalSystemIsError();
    if (result && !systemErrorReported)
    {
        systemErrorReported = true;
        log_warn("Namco N2: clSystemN2 latched a system error - the test menu "
                 "will report PCB ERROR from here on");
    }
    return result;
}

using SetString = void (*)(void *, const char *, bool);
SetString originalSetString = nullptr;
const void *pcbErrorDrawnFrom = nullptr;

void traceSetString(void *self, const char *text, bool flag)
{
    if (text && std::strstr(text, "PCB ERROR"))
    {
        const void *from = __builtin_return_address(0);
        if (from != pcbErrorDrawnFrom)
        {
            pcbErrorDrawnFrom = from;
            log_warn("Namco N2: \"PCB ERROR\" drawn, called from %p", from);
        }
    }

    originalSetString(self, text, flag);
}
} // namespace

namespace
{
/* The Wangan titles all carry gRomInfo and clSystemN2 and differ only in the
 * revision string, so each table row tests its own prefix against this. */
const char *wanganRevision()
{
    RomInfo *romInfo = static_cast<RomInfo *>(n2ResolveSymbol("gRomInfo"));
    void *systemMarker = n2ResolveSymbol("_ZN10clSystemN212initSystemN2Ev");
    if (!romInfo || !systemMarker ||
        !isPrintableString(romInfo->revisionName, sizeof(romInfo->revisionName)))
        return nullptr;
    return romInfo->revisionName;
}

int acceptRevision(const char *revision)
{
    n2SetDetectedRevision(revision);
    return 1;
}
} // namespace

extern "C" int n2Wmmt3Detect(const char *)
{
    const char *revision = wanganRevision();
    return revision && std::strstr(revision, "WM3100") ? acceptRevision(revision) : 0;
}

extern "C" int n2Wmmt3dxPlusDetect(const char *)
{
    const char *revision = wanganRevision();
    return revision && std::strncmp(revision, "W3P", 3) == 0 ? acceptRevision(revision) : 0;
}

extern "C" int n2Wmmt3dxDetect(const char *)
{
    const char *revision = wanganRevision();
    return revision && std::strncmp(revision, "W3X", 3) == 0 ? acceptRevision(revision) : 0;
}

extern "C" int n2Wmmt3FamilyDetect(const char *)
{
    const char *revision = wanganRevision();
    return revision ? acceptRevision(revision) : 0;
}

extern "C" int n2Wmmt3ShouldBlit(void)
{
    // Read the WMMT frame-ready flag.
    static void **graphicsSlot = nullptr;
    static bool lookedUp = false;
    if (!lookedUp)
    {
        graphicsSlot = reinterpret_cast<void **>(n2ResolveSymbol(
            "_ZN11teSingletonI10clGraphicsE11sm_instanceE"));
        lookedUp = true;
    }

    void *graphics = graphicsSlot ? *graphicsSlot : nullptr;
    if (!graphics)
        return 1;

    const uint8_t *state = static_cast<const uint8_t *>(graphics);
    if (n2TitleIs(N2_TITLE_ID_WMMT3))
        return *reinterpret_cast<const uint16_t *>(state + 0x48) != 0 ? 1 : 0;

    if (*reinterpret_cast<const uint16_t *>(state + 0x54) != 0)
        return 1;

    const uint32_t *buffer = *reinterpret_cast<const uint32_t *const *>(state + 0x0c);
    if (!buffer)
        buffer = *reinterpret_cast<const uint32_t *const *>(state + 0x08);
    return buffer && buffer[1] == 0 ? 1 : 0;
}

extern "C" int n2Wmmt3InstallHooks(void)
{
    n2HookSymbol("_ZN18clInputDeviceJamma8checkUseEv", reinterpret_cast<void *>(returnSuccess));
    n2HookSymbol("_ZN16clInputDevicePad12handleEventsEv", reinterpret_cast<void *>(returnSuccess));

    /* The game runs its own JVS master on /dev/ttyM3; the loader only answers
     * as the I/O board, which n2Jvio.cpp does. */
    log_info("Namco N2 JVS: answering the game's JVIO master on /dev/ttyM3");

    // The cabinet checks its steering board before attract mode. Stubbing that
    // check left clKickback uninitialised, so it never opened /dev/ttyM1 and the
    // cabinet stuck on PCB ERROR. The real sequence runs; n2Kickback.cpp answers.
    n2SteeringIoInstallHooks();

    // Silence is not fatal, missing openal32.dll must not abort startup.
    n2AudioInstallHooks();

    n2HookSymbolWithOriginal("_ZN10clSystemN27isErrorEv",
                             reinterpret_cast<void *>(traceSystemIsError),
                             reinterpret_cast<void **>(&originalSystemIsError));
    n2HookSymbolWithOriginal("_ZN12clSpriteFont9setStringEPKcb",
                             reinterpret_cast<void *>(traceSetString),
                             reinterpret_cast<void **>(&originalSetString));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService16requestGetStatusEv",
                             reinterpret_cast<void *>(getStatusCardDevice),
                             reinterpret_cast<void **>(&originalRequestGetStatus));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService11requestInitEb",
                             reinterpret_cast<void *>(initCardDevice),
                             reinterpret_cast<void **>(&originalRequestInit));
    n2HookSymbolWithOriginal("_ZN23clCardDeviceGameService21requestCheckDispenserEv",
                             reinterpret_cast<void *>(checkDispenserCardDevice),
                             reinterpret_cast<void **>(&originalRequestCheckDispenser));

    // Start the bridge, but do not read its initial state as a failed connect:
    // the worker may not have reached CreateFile yet. openCardPipe() logs the
    // authoritative result and keeps retrying if YaCardEmu starts later.
    (void)cardControlGetConnectionState();
    log_info("Namco N2 card: connecting /dev/ttyM2 to external YaCardEmu at %s",
             getConfig()->namcoN2.card.pipeName);

    n2HaspInstallHooks();
    n2InstallAdmHooks();

    int textureHooks = 0;
    textureHooks += n2InstallTextureDispatchHooks();
    if (textureHooks)
        log_info("Namco N2: installed SDL-owned main-thread texture dispatch (%d hooks)",
                 textureHooks);

    n2InstallAlchemyImageHooks();

    log_info("Namco N2 compatibility hooks installed");
    return 0;
}

extern "C" int n2Wmmt3HandleSystemCommand(const char *command)
{
    if (std::strncmp(command, "find ", 5) == 0 && std::strstr(command, ">/tmp/find.txt"))
    {
        struct FindCommand
        {
            const char *prefix;
            const char *directory;
            const char *extension;
        };
        const FindCommand commands[] = {
            {"find /tmp/data/target/", "tmp/data/target", ".target.gz"},
            {"find data/target/jp", "data/target/jp", ".target.gz"},
            {"find data/target/us", "data/target/us", ".target.gz"},
            {"find /tmp/data/ranking/", "tmp/data/ranking", ".rank"},
            {"find /tmp/data/maxicoin/", "tmp/data/maxicoin", ".maxicoin"},
            {"find /tmp/data/joinstar/", "tmp/data/joinstar", ".joinstar"}
        };
        for (const FindCommand &item : commands)
        {
            const size_t prefixLength = std::strlen(item.prefix);
            const size_t extensionLength = std::strlen(item.extension);
            if (std::strncmp(command, item.prefix, prefixLength) != 0)
                continue;

            std::vector<std::string> matches;
            std::error_code iteratorError;
            if (std::filesystem::exists(item.directory))
            {
                for (const auto &entry : std::filesystem::directory_iterator(item.directory, iteratorError))
                {
                    const std::string name = entry.path().filename().string();
                    if (entry.is_regular_file() && name.size() >= extensionLength &&
                        name.compare(name.size() - extensionLength, extensionLength, item.extension) == 0)
                        matches.push_back(name);
                }
            }
            std::sort(matches.begin(), matches.end());
            std::ofstream output("tmp/find.txt", std::ios::trunc | std::ios::binary);
            for (const std::string &name : matches)
                output << item.directory << "/" << name << "\n";
            return output ? 0 : 1;
        }
    }

    if (std::strcmp(command, "cp -f data/target/*.target.gz /tmp/data/target/ 2>/dev/null") == 0)
    {
        std::error_code copyError;
        std::filesystem::create_directories("tmp/data/target", copyError);
        for (const auto &entry : std::filesystem::directory_iterator("data/target", copyError))
        {
            const std::string name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() >= 10 && name.compare(name.size() - 10, 10, ".target.gz") == 0)
                std::filesystem::copy_file(entry.path(), std::filesystem::path("tmp/data/target") / name,
                                           std::filesystem::copy_options::overwrite_existing, copyError);
        }
        return 0;
    }

    if (std::strncmp(command, "perl prepend-n2.pl", 18) != 0)
        return -1;

    const char *directories[] = {
        "tmp/data/target",
        // prepend-n2.pl creates target/old alongside target itself.
        "tmp/data/target/old",
        "tmp/data/tournament",
        "tmp/data/ranking",
        "tmp/data/maxicoin",
        "tmp/data/joinstar",
        "tmp/data/card",
        "tmp/data/etc",
        "tmp/data2/tournament"
    };
    std::error_code error;
    for (const char *directory : directories)
    {
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            log_error("Namco N2: failed to prepare %s: %s", directory, error.message().c_str());
            return 1;
        }
    }

    struct CopyItem
    {
        const char *source;
        const char *destination;
    };
    const CopyItem copies[] = {
        {"data/sound/bgm/maxi3/sys_04.wav", "tmp/sys_04.wav"},
        {"data/sprite/Full_white.png", "tmp/joinshot.png"}
    };
    for (const CopyItem &item : copies)
    {
        if (std::filesystem::exists(item.source))
            std::filesystem::copy_file(item.source, item.destination, std::filesystem::copy_options::overwrite_existing, error);
        error.clear();
    }

    log_info("Namco N2: prepared virtual work disk directories");
    return 0;
}

#endif
