#include "n2CsNeo.h"

#include "../n2.h"
#include "../n2Hasp.h"
#include "../n2Hook.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cmath>
#include <cstring>
#include <filesystem>

#include <SDL3/SDL.h>

#include "../../../../elfLoader/sdl12Bridge.hpp"
#include "../../../../elfLoader/symbolResolver.hpp"
#include "../../../../log/log.h"

namespace
{
constexpr char launcherPrefix[] = "hlds";
constexpr char engineModule[] = "engine_amd.so";
constexpr char defaultLoginId[] = "12";
constexpr char localLoginCommandText[] = "login 12\n";
constexpr char contentsDirectory[] = "freespace/contents2";
constexpr char saveDirectory[] = "platform/SAVE";

int returnNoError()
{
    return 0;
}

using ChangeScene = void (*)(const char *, bool, const char *, int);
ChangeScene originalChangeScene = nullptr;

// Scenes during play whose windows are answered by clicking; the pointer has
// to stay free there.
constexpr const char *clickablePlayScenes[] = {
    "PLAY_START", "PLAY_END_PARENT", "PLAY_ALLEND_LOSE_STRIKEMISSION",
};

bool sceneUsesMouseLook(const char *scene)
{
    if (!scene || std::strncmp(scene, "PLAY", 4) != 0)
        return false;
    for (const char *clickable : clickablePlayScenes)
    {
        if (std::strcmp(scene, clickable) == 0)
            return false;
    }
    return true;
}

void changeScene(const char *scene, bool flag, const char *file, int line)
{
    // Only a flag is set here, on the guest's thread; the bridge applies it
    // where it owns the window.
    Sdl12Bridge::setMouseCaptured(sceneUsesMouseLook(scene));
    originalChangeScene(scene, flag, file, line);
}

/*
 * engine_amd.so was built for the cabinet's Athlon and writes these two with
 * 3DNow!, which no current CPU executes: the first bullet impact's particles
 * died on `femms` (illegal instruction).  Same arithmetic in plain C.
 */
void applyVectorMatrix(float *out, float *matrix, float *vector)
{
    // A row vector times a row-major 4x4; the input is copied first because
    // callers may pass the same array for both.
    const float in[4] = {vector[0], vector[1], vector[2], vector[3]};
    for (int column = 0; column < 4; ++column)
        out[column] = in[0] * matrix[column] + in[1] * matrix[4 + column] +
                      in[2] * matrix[8 + column] + in[3] * matrix[12 + column];
}

void normalizeVector(float *out, float *in)
{
    // xyz scaled to unit length; w is carried through untouched.  3DNow!'s
    // reciprocal square root of zero is the largest float, so a zero vector
    // stays zero rather than becoming NaN.
    const float w = in[3];
    const float lengthSquared = in[0] * in[0] + in[1] * in[1] + in[2] * in[2];
    const float scale = lengthSquared > 0.0f ? 1.0f / std::sqrt(lengthSquared) : 0.0f;
    out[0] = in[0] * scale;
    out[1] = in[1] * scale;
    out[2] = in[2] * scale;
    out[3] = w;
}

/*
 * One slot of the engine's `net_return` table: a request to the store server
 * registers its reply callback here, and net_frame() calls it with -1 once
 * `timeout` ms have passed without a reply.
 */
struct NetReturnSlot
{
    int state;
    uint32_t start;
    uint32_t timeout;
    void (*callback)(int);
};
constexpr int netReturnSlots = 8;

void expireStoreServerRequest(unsigned char command, char *)
{
    /*
     * makeApdu() sends every request to the store server, which offline play
     * does not have.  Left alone, the reply wait runs out ten seconds later and
     * its callback still switches scene - after name entry that is the trial
     * menu, so a player who has meanwhile started a map is thrown back out of
     * it.  Expire the wait now instead, so the game's own no-reply path runs on
     * the next frame, where the cabinet would have reached it without a server.
     */
    static NetReturnSlot *slots = nullptr;
    if (!slots)
        slots = static_cast<NetReturnSlot *>(n2ResolveSymbol("net_return"));
    if (!slots)
        return;

    for (int i = 0; i < netReturnSlots; ++i)
    {
        if (slots[i].callback && slots[i].timeout)
        {
            log_debug("Namco N2 CS Neo: store-server request %u has no server; "
                      "expiring its reply wait (slot %d)", command, i);
            slots[i].timeout = 0;
        }
    }
}

void localLoginCommand(void *, int argc, char **argv)
{
    const char *loginId = argc > 1 && argv && argv[1] ? argv[1] : defaultLoginId;
    using MsLogin = void (*)(const char *);
    MsLogin localLogin = reinterpret_cast<MsLogin>(n2ResolveSymbol("_Z7msLoginPKc"));
    if (!localLogin)
    {
        log_warn("Namco N2 CS Neo: could not resolve msLogin for offline login");
        return;
    }

    localLogin(loginId);
    log_info("Namco N2 CS Neo: local login %s accepted without store DB", loginId);
}

void createDirectory(const char *path)
{
    std::error_code failure;
    std::filesystem::create_directories(path, failure);
    if (failure)
        log_warn("Namco N2 CS Neo: could not create %s (%s)",
                 path, failure.message().c_str());
}
} // namespace

extern "C" int n2CsNeoLooksLikeGame(const char *elfPath)
{
    if (!elfPath || !*elfPath)
        return 0;

    std::error_code failure;
    const std::filesystem::path executable(elfPath);
    if (executable.stem().string().rfind(launcherPrefix, 0) != 0)
        return 0;

    return std::filesystem::exists(executable.parent_path() / engineModule, failure) ? 1 : 0;
}

extern "C" int n2CsNeoPrepareLoad(const char *elfPath)
{
    if (!n2CsNeoLooksLikeGame(elfPath))
        return 0;

    /*
     * engine_amd.so is a DT_NEEDED dependency of the stripped launcher, so
     * its PLT is relocated before game detection can inspect the mapped ELF.
     * Reserve these path-identifiable overrides before loading either file.
     */
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z10IsPCBErrorv", reinterpret_cast<void *>(returnNoError));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z10IsTestModev", reinterpret_cast<void *>(returnNoError));
    // makeError() must stay the game's own: it builds every error.csv window,
    // including the player-name confirmation, not only cabinet errors.  The
    // store-server errors it used to be muted for no longer occur under -nodb.
    SymbolResolver::GetInstance().RegisterVTable(
        "_ZN12CommandLogin7ExecuteEiPPc", reinterpret_cast<void *>(localLoginCommand));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z8makeApduhPc", reinterpret_cast<void *>(expireStoreServerRequest));
    // Mouse-look turns by relative motion; in a window the pointer would
    // otherwise run into the edge and out of it.  The engine is dlopened after
    // start-up, so this is bound through its PLT rather than hooked later.
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z12_changeScenePKcbS0_i", reinterpret_cast<void *>(changeScene),
        reinterpret_cast<void **>(&originalChangeScene));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z17ApplyVectorMatrixPfS_S_", reinterpret_cast<void *>(applyVectorMatrix));
    SymbolResolver::GetInstance().RegisterVTable(
        "_Z15NormalizeVectorPfS_", reinterpret_cast<void *>(normalizeVector));
    n2RegisterHaspPreloadOverrides();
    return 1;
}

extern "C" int n2CsNeoInstallHooks(void)
{
    createDirectory(contentsDirectory);
    createDirectory(saveDirectory);

    n2InstallAdmHooks();
    log_info("Namco N2 CS Neo compatibility hooks installed");
    return 0;
}

extern "C" const char *const *n2CsNeoRequiredArguments(void)
{
    /*
     * `-game czero` is the mod directory; without it the first texture lookup
     * is fatal.  `-nodb` is the game's own store-server-less mode: initializeThread
     * then hands DB::Init an empty host, and every dbcommand::*::Execute takes
     * its built-in local branch instead of an Axis SOAP call to <own IP>.241.
     * Without it, login (msLogin -> DB::TrialLogin) never completes, so the
     * tutorial and every mode stay unreachable.  `-noms` makes the login reply
     * callback (dbcallback.cpp) accept a missing store-server answer outside
     * trial mode too, instead of ending the game with "login TIMEOUT".
     */
    static const char *const arguments[] = {"-game czero", "-nodb", "-noms", nullptr};
    return arguments;
}

extern "C" int n2CsNeoHandleHostKey(int key, uint32_t modifiers)
{
    if ((modifiers & SDL_KMOD_ALT) == 0 || key != SDLK_F7)
        return 0;

    using CbufAddText = void (*)(const char *);
    CbufAddText addText = reinterpret_cast<CbufAddText>(n2ResolveSymbol("Cbuf_AddText"));
    if (!addText)
    {
        log_warn("Namco N2 CS Neo: could not resolve Cbuf_AddText for local login");
        return 1;
    }

    // Keep the established offline `login 12` procedure available for dumps
    // carrying the matching local database patches.
    addText(localLoginCommandText);
    log_info("Namco N2 CS Neo: requested local login 12 (Alt+F7)");
    return 1;
}

extern "C" int n2CsNeoDetect(const char *elfPath)
{
    return n2CsNeoLooksLikeGame(elfPath);
}

#endif
