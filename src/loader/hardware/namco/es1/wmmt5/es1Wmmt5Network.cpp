#include "es1Wmmt5Network.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include <cstdint>
#include <cstring>
#include <windows.h>

#include "../es1CompatLayer.h"
#include "../../../../log/log.h"

/* Answers for the live network WMN5r reads through before it draws anything;
 * without them its network objects stay null and it faults on one. */
namespace
{

constexpr uintptr_t ContentRouterAddress = 0x082519d0;
constexpr uintptr_t NetworkStateAddress = 0x084ec560;
constexpr uintptr_t LinkCheckAddress = 0x0aa75120;
constexpr uintptr_t PeerCheckAddress = 0x0a393be0;
constexpr uintptr_t BillingSaveAddress = 0x08401a70;
constexpr uintptr_t DecryptTokenAddress = 0x0aa77780;

/* Each signature runs to the end of its own prologue. */
constexpr uint8_t ContentRouterSignature[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x5d, 0x8b, 0x00};
constexpr uint8_t NetworkStateSignature[] = {0x55, 0x89, 0xe5, 0x56, 0x53, 0x83, 0xec, 0x10};
constexpr uint8_t LinkCheckSignature[] = {0x55, 0x89, 0xe5, 0x53, 0x83, 0xec, 0x04, 0x8b, 0x45, 0x08};
constexpr uint8_t PeerCheckSignature[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x89, 0x5d, 0xf8};
constexpr uint8_t BillingSaveSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0xcc, 0x00};
constexpr uint8_t DecryptTokenSignature[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0x3c, 0x14};

/* Two jumps decide whether a locally supplied content router is accepted;
 * there is no function boundary to hook, so they are patched. */
constexpr uintptr_t RouterAcceptAddress = 0x0827f7e3;
constexpr uintptr_t RouterRejectAddress = 0x0827f9fc;
constexpr uint8_t RouterAcceptExpected[] = {0x0f, 0x84};
constexpr uint8_t RouterAcceptPatched[] = {0x0f, 0x85};
constexpr uint8_t RouterRejectExpected[] = {0x0f, 0x85};
constexpr uint8_t RouterRejectPatched[] = {0x0f, 0x84};

/* The live hostname in .rodata, replaced with one that resolves locally. */
constexpr uintptr_t MuchaHostAddress = 0x0aafaa88;
constexpr char MuchaHostExpected[] = "v388-front.mucha-prd.nbgi-amnet.jp";
constexpr char MuchaHostLocal[] = "mucha.local";

int (*g_originalDecryptToken)(char *, int *, char *, void *) = nullptr;

/* The cabinet's own subnet; .254 is where the router sits on an ES1 network. */
uint32_t contentRouterAddress(void)
{
    return 0xfe5ca8c0u; /* 192.168.92.254, network byte order */
}

int wmmt5ContentRouter(void) { return static_cast<int>(contentRouterAddress()); }

/* The caller wants a link-state word; zero is "nothing wrong here". */
void wmmt5NetworkState(int *state)
{
    if (state)
        *state = 0;
}

int wmmt5LinkCheck(void) { return 0; }
int wmmt5PeerCheck(void) { return 0; }

/* Billing has no server to save to, and writing the record is what crashes. */
void wmmt5BillingSave(void) {}

/* A token marked with a leading '@' passes through unencrypted. */
int wmmt5DecryptToken(char *destination, int *destinationSize, char *source, void *context)
{
    if (source && source[0] == '@')
    {
        const size_t length = std::strlen(source + 1);
        std::memcpy(destination, source + 1, length);
        if (destinationSize)
            *destinationSize = static_cast<int>(length);
        return 0;
    }
    if (!g_originalDecryptToken)
        return -1;
    return g_originalDecryptToken(destination, destinationSize, source, context);
}

/* Writes over guest code or data after checking what is already there. */
bool patchGuest(uintptr_t address, const void *expected, const void *replacement, size_t length,
                const char *name)
{
    void *target = reinterpret_cast<void *>(address);
    if (std::memcmp(target, expected, length) != 0)
    {
        log_error("WMMT5: %s is not where expected at %p; leaving it alone", name, target);
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &previous))
    {
        log_error("WMMT5: could not unprotect %s at %p", name, target);
        return false;
    }
    std::memcpy(target, replacement, length);
    VirtualProtect(target, length, previous, &previous);
    log_info("System ES1 WMMT5 network: patched %s at %p", name, target);
    return true;
}

} // namespace

void es1Wmmt5InstallNetworkHooks(void)
{
    const Es1HookSpec hooks[] = {
        {ContentRouterAddress, reinterpret_cast<void *>(wmmt5ContentRouter), "contentRouter",
         nullptr, ContentRouterSignature, sizeof(ContentRouterSignature)},
        {NetworkStateAddress, reinterpret_cast<void *>(wmmt5NetworkState), "networkState", nullptr,
         NetworkStateSignature, sizeof(NetworkStateSignature)},
        {LinkCheckAddress, reinterpret_cast<void *>(wmmt5LinkCheck), "linkCheck", nullptr,
         LinkCheckSignature, sizeof(LinkCheckSignature)},
        {PeerCheckAddress, reinterpret_cast<void *>(wmmt5PeerCheck), "peerCheck", nullptr,
         PeerCheckSignature, sizeof(PeerCheckSignature)},
        {BillingSaveAddress, reinterpret_cast<void *>(wmmt5BillingSave), "billingSave", nullptr,
         BillingSaveSignature, sizeof(BillingSaveSignature)},
        {DecryptTokenAddress, reinterpret_cast<void *>(wmmt5DecryptToken), "decryptToken",
         reinterpret_cast<void **>(&g_originalDecryptToken), DecryptTokenSignature,
         sizeof(DecryptTokenSignature)},
    };
    es1InstallHookTable(hooks, sizeof(hooks) / sizeof(hooks[0]), "WMMT5 network");

    patchGuest(RouterAcceptAddress, RouterAcceptExpected, RouterAcceptPatched,
               sizeof(RouterAcceptExpected), "content router accept");
    patchGuest(RouterRejectAddress, RouterRejectExpected, RouterRejectPatched,
               sizeof(RouterRejectExpected), "content router reject");
    patchGuest(MuchaHostAddress, MuchaHostExpected, MuchaHostLocal, sizeof(MuchaHostLocal),
               "mucha hostname");
}

#endif
