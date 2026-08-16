#if defined(_WIN32) || defined(__MINGW32__)

#include "es1OnlineNetwork.hpp"

#include "../../../config/config.h"
#include "../../../log/log.h"

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

/* Service names recovered from the binaries; "mucha.local" is kept as an alias
 * so existing DNS_MUCHA_LOCAL settings still mean what was intended. */
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

extern "C" const char *es1RedirectedHost(const char *name)
{
    if (!name)
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
                log_info("System ES1 DNS redirect: %s -> %s", redirect.name, redirect.target);
            return redirect.target;
        }
    }
    return name;
}

extern "C" const char *es1CaCertificatePath(void)
{
    static const std::string path = [] {
        std::error_code error;
        const std::filesystem::path gameDirectory =
            std::filesystem::path(g_absoluteElfPath).parent_path();
        /* The network tree lives in whichever package shipped it. */
        static const char *const roots[] = {"data", "data_en", "data_ng_lnx"};
        for (const char *root : roots)
        {
            std::filesystem::path file =
                gameDirectory / root / "network" / "certs" / "v388-ca-cert.pem";
            if (!std::filesystem::exists(file, error))
                continue;
            /* The guest chdir()s, so libcurl needs an absolute path. */
            const std::filesystem::path absolute = std::filesystem::absolute(file, error);
            if (!error)
                file = absolute;
            return file.string();
        }
        return std::string();
    }();
    return path.empty() ? nullptr : path.c_str();
}

extern "C" void es1ConfigureCurlHandle(void *handle, Es1CurlSetopt setopt,
                                       Es1CurlSlistAppend slistAppend)
{
    if (!handle || !setopt)
        return;

    /* The host bundle alone fails every request with
     * CURLE_PEER_FAILED_VERIFICATION; use the file the title's OpenSSL loads. */
    if (const char *ca = es1CaCertificatePath())
    {
        const int result = setopt(handle, CurlOptCaInfo, ca);
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true))
            log_info("System ES1 network: curl trusts the cabinet's CA bundle %s (-> %d)", ca,
                     result);
    }

    /* libcurl resolves the URL itself, so CURLOPT_RESOLVE pins the address while
     * the Host header, SNI name and certificate check keep the real name. */
    if (void *resolve = [slistAppend]() -> void * {
            static void *list = nullptr;
            static std::atomic<bool> built{false};
            if (built.exchange(true) || !slistAppend)
                return list;
            Redirect redirects[8];
            const size_t count =
                collectRedirects(redirects, sizeof(redirects) / sizeof(redirects[0]));
            /* Mucha 10082, ALL.Net 80, game server 9002 or 443 in practice. */
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
                log_info("System ES1 network: curl pins %s -> %s", redirect.name,
                         redirect.target);
            }
            return list;
        }())
    {
        setopt(handle, CurlOptResolve, resolve);
    }

    /* The cabinet links OpenSSL 0.9.8, whose SHA-1 1024-bit root a modern
     * libcurl calls too weak; verification itself stays on. */
    const int cipherResult = setopt(handle, CurlOptSslCipherList, "DEFAULT:@SECLEVEL=0");
    static std::atomic<bool> cipherReported{false};
    if (!cipherReported.exchange(true))
        log_info("System ES1 network: curl security level lowered to the cabinet's OpenSSL 0.9.8"
                 " (-> %d)",
                 cipherResult);

    if (std::getenv("LL_CURL_VERBOSE"))
        setopt(handle, CurlOptVerbose, 1L);
}

#endif
