#if defined(_WIN32) || defined(__MINGW32__)

#include "es1Wmmt4Network.hpp"

#include "../es1.h"
#include "../es1CompatLayer.h"
#include "../es1Title.h"
#include "es1Wmmt4.h"
#include "../../../../config/config.h"
#include "../../../../elfLoader/guestTls.hpp"
#include "../../../../elfLoader/symbolResolver.hpp"
#include "../../../../log/log.h"

#include <atomic>
#include <cstdint>
#include <cstring>
/* Boost.Asio TLS engine trace (LL_WMMT4_TLS_TRACE=1).  The title drives OpenSSL
 * through a BIO pair, so logging engine::perform()'s primitive and its "want"
 * is the only way to see whether the handshake asked for a write. */
namespace
{
constexpr uintptr_t EnginePerformAddress = 0x830d4a0;
constexpr uintptr_t DoConnectAddress = 0x830c910;

struct GuestPrimitive
{
    uintptr_t address;
    const char *name;
};

const GuestPrimitive PrimitiveNames[] = {
    {0x830c910, "SSL_connect"},
    {0x830eed0, "SSL_accept"},
    {0x830c930, "SSL_accept(nolock)"},
    {0x830c8e0, "SSL_write"},
    {0x830c960, "SSL_read"},
};

/* The engine's own view, read back through the title's OpenSSL so the numbers
 * are the ones it acted on.  It decides want_output purely from
 * BIO_ctrl_pending() growing across the call. */
struct GuestSslApi
{
    int (*getError)(void *ssl, int result) = nullptr;
    unsigned long (*peekError)(void) = nullptr;
    int (*state)(const void *ssl) = nullptr;
    const char *(*stateStringLong)(const void *ssl) = nullptr;
    long (*bioPending)(void *bio) = nullptr;
    int (*getShutdown)(const void *ssl) = nullptr;
};

const GuestSslApi &guestSsl()
{
    static const GuestSslApi api = [] {
        GuestSslApi resolved;
        resolved.getError = reinterpret_cast<int (*)(void *, int)>(
            bridgeResolveSymbolOptional("SSL_get_error"));
        resolved.peekError = reinterpret_cast<unsigned long (*)()>(
            bridgeResolveSymbolOptional("ERR_peek_error"));
        resolved.state =
            reinterpret_cast<int (*)(const void *)>(bridgeResolveSymbolOptional("SSL_state"));
        resolved.stateStringLong = reinterpret_cast<const char *(*)(const void *)>(
            bridgeResolveSymbolOptional("SSL_state_string_long"));
        resolved.bioPending =
            reinterpret_cast<long (*)(void *)>(bridgeResolveSymbolOptional("BIO_ctrl_pending"));
        resolved.getShutdown = reinterpret_cast<int (*)(const void *)>(
            bridgeResolveSymbolOptional("SSL_get_shutdown"));
        log_info("WMMT4 TLS trace: guest OpenSSL get_error=%p peek_error=%p state=%p "
                 "state_string=%p bio_pending=%p",
                 reinterpret_cast<void *>(resolved.getError),
                 reinterpret_cast<void *>(resolved.peekError),
                 reinterpret_cast<void *>(resolved.state),
                 reinterpret_cast<void *>(resolved.stateStringLong),
                 reinterpret_cast<void *>(resolved.bioPending));
        return resolved;
    }();
    return api;
}

typedef int (*EngineOpFn)(void *engine);
EngineOpFn g_originalDoConnect = nullptr;
std::atomic<unsigned int> g_connectTraceCount{0};

extern "C" int wmmt4EngineDoConnect(void *engine)
{
    GuestTls::HostCallScope hostCall;

    const GuestSslApi &ssl = guestSsl();
    void *const handle = engine ? *reinterpret_cast<void **>(engine) : nullptr;
    void *const bio = engine ? reinterpret_cast<void **>(engine)[1] : nullptr;

    GuestTls::EnterGuestCode();
    const long pendingBefore = ssl.bioPending && bio ? ssl.bioPending(bio) : -1;
    const int stateBefore = ssl.state && handle ? ssl.state(handle) : -1;
    const int result = g_originalDoConnect(engine);
    const int sslError = ssl.getError && handle ? ssl.getError(handle, result) : -1;
    const unsigned long queued = ssl.peekError ? ssl.peekError() : 0;
    const long pendingAfter = ssl.bioPending && bio ? ssl.bioPending(bio) : -1;
    const int stateAfter = ssl.state && handle ? ssl.state(handle) : -1;
    const char *description =
        ssl.stateStringLong && handle ? ssl.stateStringLong(handle) : nullptr;
    GuestTls::EnterHostCall();

    const unsigned int seen = g_connectTraceCount.fetch_add(1) + 1;
    if (seen <= 40)
        log_info("LLTLS SSL_connect ssl=%p bio=%p -> %d ssl_error=%d err=0x%08lx "
                 "pending %ld->%ld state 0x%x->0x%x (%s)",
                 handle, bio, result, sslError, queued, pendingBefore, pendingAfter,
                 stateBefore, stateAfter, description ? description : "?");
    return result;
}

const char *primitiveName(const void *function)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(function);
    for (const GuestPrimitive &primitive : PrimitiveNames)
        if (primitive.address == address)
            return primitive.name;
    return "?";
}

typedef int (*EnginePerformFn)(void *engine, void *function, int adjust, void *data,
                               unsigned int length, int *error, unsigned int *transferred);
EnginePerformFn g_originalEnginePerform = nullptr;
std::atomic<unsigned int> g_performTraceCount{0};

/* The guest passes the primitive as a pointer to member, so `function` is
 * either the address itself or, with bit 0 set, a vtable byte offset plus one. */
extern "C" int wmmt4EnginePerform(void *engine, void *function, int adjust, void *data,
                                  unsigned int length, int *error, unsigned int *transferred)
{
    GuestTls::HostCallScope hostCall;

    GuestTls::EnterGuestCode();
    const int want = g_originalEnginePerform(engine, function, adjust, data, length, error,
                                             transferred);
    GuestTls::EnterHostCall();

    const unsigned int seen = g_performTraceCount.fetch_add(1) + 1;
    if (seen <= 200)
        log_info("LLTLS engine=%p op=%p(%s) len=%u -> want=%d ec=%d ssl=%p bytes=%u", engine,
                 function, primitiveName(function), length, want, error ? *error : 0,
                 engine ? *reinterpret_cast<void **>(engine) : nullptr,
                 transferred ? *transferred : 0u);
    return want;
}
} // namespace

extern "C" void wmmt4InstallNetworkDiagnostics(void)
{
    if (!std::getenv("LL_WMMT4_TLS_TRACE"))
        return;

    const Es1HookSpec hooks[] = {
        {EnginePerformAddress, reinterpret_cast<void *>(wmmt4EnginePerform),
         "asio_ssl_engine_perform", reinterpret_cast<void **>(&g_originalEnginePerform)},
        {DoConnectAddress, reinterpret_cast<void *>(wmmt4EngineDoConnect),
         "asio_ssl_engine_do_connect", reinterpret_cast<void **>(&g_originalDoConnect)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT4 TLS trace");
}

#endif
