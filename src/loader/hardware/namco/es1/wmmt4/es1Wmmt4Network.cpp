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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

extern std::string g_absoluteElfPath;

namespace
{
bool dnsNameEquals(const char *name, const char *expected)
{
    if (!name || !expected)
        return false;
    size_t length = std::strlen(name);
    /* A fully qualified name may arrive with the root label attached. */
    if (length && name[length - 1] == '.')
        --length;
    return length == std::strlen(expected) && _strnicmp(name, expected, length) == 0;
}

struct Redirect
{
    const char *name;
    const char *target;
    uint32_t reportBit;
};

/* The service names the title asks for, recovered from the binary.
 * "mucha.local" is not among them and never matched anything; it is kept as an
 * alias so existing DNS_MUCHA_LOCAL settings still mean what was intended. */
size_t collectRedirects(Redirect *out, size_t capacity)
{
    const NamcoES1Config &config = getConfig()->namcoES1;
    const Redirect all[] = {
        {"nbgi.loc", config.dnsNbgiLoc, 1u << 0},
        {"tenporouter.loc", config.dnsTenporouterLoc, 1u << 1},
        {"bbrouter.loc", config.dnsBbrouterLoc, 1u << 2},
        {"mucha.local", config.dnsMuchaLocal, 1u << 3},
        {"v388-front.mucha-prd.nbgi-amnet.jp", config.dnsMuchaLocal, 1u << 5},
        {"v388-front.mucha-prd.bandainamcogames.cn", config.dnsMuchaLocal, 1u << 6},
        {"naominet.jp", config.dnsNaominetJp, 1u << 4},
    };
    size_t count = 0;
    for (const Redirect &redirect : all)
    {
        if (count >= capacity)
            break;
        out[count++] = redirect;
    }
    return count;
}

constexpr int CurlOptCaInfo = 10065;
constexpr int CurlOptResolve = 10203;
constexpr int CurlOptSslCipherList = 10083;
constexpr int CurlOptVerbose = 41;
} // namespace

extern "C" const char *wmmt4RedirectedHost(const char *name)
{
    if (!name || !es1TitleIs(ES1_TITLE_ID_WMMT4))
        return name;

    Redirect redirects[8];
    const size_t count = collectRedirects(redirects, sizeof(redirects) / sizeof(redirects[0]));
    static std::atomic<uint32_t> reported{0};
    for (size_t index = 0; index < count; ++index)
    {
        const Redirect &redirect = redirects[index];
        if (redirect.target[0] && dnsNameEquals(name, redirect.name))
        {
            const uint32_t previous = reported.fetch_or(redirect.reportBit,
                                                        std::memory_order_relaxed);
            if ((previous & redirect.reportBit) == 0)
                log_info("WMMT4 DNS redirect: %s -> %s", redirect.name, redirect.target);
            return redirect.target;
        }
    }
    return name;
}

extern "C" const char *wmmt4CaCertificatePath(void)
{
    static const std::string path = [] {
        std::error_code error;
        std::filesystem::path file =
            std::filesystem::path(g_absoluteElfPath).parent_path() /
            "data" / "Network" / "certs" / "v388-ca-cert.pem";
        if (!std::filesystem::exists(file, error))
            return std::string();
        /* The guest chdir()s, so libcurl needs an absolute path. */
        const std::filesystem::path absolute = std::filesystem::absolute(file, error);
        if (!error)
            file = absolute;
        return file.string();
    }();
    return path.empty() ? nullptr : path.c_str();
}

extern "C" void wmmt4ConfigureCurlHandle(void *handle, Wmmt4CurlSetopt setopt,
                                         Wmmt4CurlSlistAppend slistAppend)
{
    if (!handle || !setopt || !es1TitleIs(ES1_TITLE_ID_WMMT4))
        return;

    /* Point curl at the CA file the title's own OpenSSL side loads, so both
     * halves agree on who to trust; the host bundle alone fails every request
     * with CURLE_PEER_FAILED_VERIFICATION.  A private server adds its root
     * to that same file. */
    if (const char *ca = wmmt4CaCertificatePath())
    {
        const int result = setopt(handle, CurlOptCaInfo, ca);
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true))
            log_info("WMMT4 network: curl trusts the title's CA bundle %s (-> %d)", ca, result);
    }

    /* libcurl resolves the URL itself, so the loader's DNS_* redirects never see
     * it.  CURLOPT_RESOLVE pins the address while leaving the URL alone, so the
     * Host header, SNI name and certificate check all keep the real hostname. */
    if (void *resolve = [slistAppend] () -> void * {
            static void *list = nullptr;
            static std::atomic<bool> built{false};
            if (built.exchange(true) || !slistAppend)
                return list;
            Redirect redirects[8];
            const size_t count =
                collectRedirects(redirects, sizeof(redirects) / sizeof(redirects[0]));
            /* Mucha is 10082, ALL.Net 80; the game server port arrives in the
             * ALL.Net PowerOn reply and has been 9002 or 443 in practice. */
            const int ports[] = {80, 443, 9002, 10082};
            for (size_t index = 0; index < count; ++index)
            {
                const Redirect &redirect = redirects[index];
                if (!redirect.target[0])
                    continue;
                for (const int port : ports)
                {
                    char entry[256];
                    std::snprintf(entry, sizeof(entry), "%s:%d:%s", redirect.name, port,
                                  redirect.target);
                    if (void *next = slistAppend(list, entry))
                        list = next;
                }
                log_info("WMMT4 network: curl pins %s -> %s", redirect.name, redirect.target);
            }
            return list;
        }())
    {
        setopt(handle, CurlOptResolve, resolve);
    }

    /* The cabinet links OpenSSL 0.9.8, where a SHA-1 signed 1024-bit root is
     * ordinary; a modern libcurl calls that key too weak.  Drop to the security
     * level the title's own TLS stack has - verification itself stays on. */
    const int cipherResult = setopt(handle, CurlOptSslCipherList, "DEFAULT:@SECLEVEL=0");
    static std::atomic<bool> cipherReported{false};
    if (!cipherReported.exchange(true))
        log_info("WMMT4 network: curl security level lowered to the cabinet's OpenSSL 0.9.8 (-> %d)",
                 cipherResult);

    if (std::getenv("LL_CURL_VERBOSE"))
        setopt(handle, CurlOptVerbose, 1L);
}

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
