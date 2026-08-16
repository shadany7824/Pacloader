#include "es1Wmmt5.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <filesystem>

#include "es1Wmmt5Cabinet.hpp"
#include "es1Wmmt5Card.hpp"
#include "es1Wmmt5Dongle.hpp"
#include "es1Wmmt5Log.hpp"
#include "es1Wmmt5Network.hpp"
#include "es1Wmmt5Steering.hpp"
#include "es1Wmmt5Vendor.hpp"
#include "../../../../redirections/filesystem.h"

/* TODO: WMMT5DX+ is unfinished. It boots and runs, but the steering check
 * never completes and mucha authentication needs a local server. */

extern "C" int es1Wmmt5dxPlusDetect(const char *elfPath)
{
    const std::filesystem::path elf(elfPath);
    const std::filesystem::path gameDir = elf.parent_path();

    /* The wangan4_* scripts identify the ES1 Wangan package; data_en and
     * data_ng_lnx separate this export build from the Japanese ones. */
    return elf.filename() == "WMN5r" &&
                   std::filesystem::exists(gameDir / "wangan4_exec") &&
                   std::filesystem::exists(gameDir / "wangan4_storage") &&
                   std::filesystem::exists(gameDir / "data") &&
                   std::filesystem::exists(gameDir / "data_en") &&
                   std::filesystem::exists(gameDir / "data_ng_lnx")
               ? 1
               : 0;
}

extern "C" int es1Wmmt5dxPlusInstallHooks(void)
{
    /* The cabinet mounts three packages over one another; extracted they stay
     * apart, so the two specific trees are searched before the base one. */
    static const char *const dataRoots[] = {"data_en", "data_ng_lnx"};
    redirectSetDataOverlay(dataRoots, sizeof(dataRoots) / sizeof(dataRoots[0]));

    es1Wmmt5InstallLogHooks();
    es1Wmmt5InstallDongleHooks();
    es1Wmmt5InstallNetworkHooks();
    es1Wmmt5InstallSteeringHooks();
    es1Wmmt5InstallCardHooks();
    es1Wmmt5InstallVendorHooks();
    es1Wmmt5InstallCabinetHooks();
    return 0;
}

#endif
