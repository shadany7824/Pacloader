#include "es1Wmmt5.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include <filesystem>
#include <fstream>
#include <string>

#include "es1Wmmt5Build.hpp"
#include "es1Wmmt5Cabinet.hpp"
#include "es1Wmmt5Card.hpp"
#include "es1Wmmt5Dongle.hpp"
#include "es1Wmmt5Log.hpp"
#include "es1Wmmt5Network.hpp"
#include "es1Wmmt5Steering.hpp"
#include "es1Wmmt5Vendor.hpp"
#include "../es1Title.h"
#include "../../../../log/log.h"
#include "../../../../redirections/filesystem.h"

namespace
{

/* The wangan4_* scripts identify the ES1 Wangan package; the localized tree
 * beside data says which market it was built for. */
bool isWanganPackage(const std::filesystem::path &elf, const char *localizedTree)
{
    const std::filesystem::path gameDir = elf.parent_path();
    return elf.filename() == "WMN5r" && std::filesystem::exists(gameDir / "wangan4_exec") &&
           std::filesystem::exists(gameDir / "wangan4_storage") &&
           std::filesystem::exists(gameDir / "data") &&
           std::filesystem::exists(gameDir / localizedTree) &&
           std::filesystem::exists(gameDir / "data_ng_lnx");
}

/* The package's own version file: one line such as "2.22.01". */
std::string packageVersion(const std::filesystem::path &elf)
{
    std::ifstream file(elf.parent_path() / "info");
    std::string version;
    std::getline(file, version);
    while (!version.empty() && (version.back() == '\r' || version.back() == '\n' ||
                                version.back() == ' '))
        version.pop_back();
    return version;
}

/* Both China packages carry data_cn; the version tells them apart: 5DX+ is
 * 2.2x, 5DX is 2.1x. */
int detectChina(const char *elfPath, bool plus)
{
    const std::filesystem::path elf(elfPath);
    if (!isWanganPackage(elf, "data_cn"))
        return 0;
    const std::string version = packageVersion(elf);
    if ((version.rfind("2.2", 0) == 0) != plus)
        return 0;
    es1SetDetectedRevision(version.c_str());
    return 1;
}

} // namespace

extern "C" int es1Wmmt5dxPlusDetect(const char *elfPath)
{
    return isWanganPackage(elfPath, "data_en") ? 1 : 0;
}

extern "C" int es1Wmmt5dxPlusChinaDetect(const char *elfPath)
{
    return detectChina(elfPath, true);
}

extern "C" int es1Wmmt5dxChinaDetect(const char *elfPath)
{
    return detectChina(elfPath, false);
}

extern "C" int es1Wmmt5InstallHooks(void)
{
    const Wmmt5Build *build = es1Wmmt5Build();
    if (!build)
        return 0;
    log_info("System ES1 WMMT5: using the %s address table", build->name);

    /* The cabinet mounts three packages over one another; extracted they stay
     * apart, so the localized and Linux-specific trees precede the base one. */
    const bool china = !es1TitleIs(ES1_TITLE_ID_WMMT5DXP);
    const char *const dataRoots[] = {china ? "data_cn" : "data_en", "data_ng_lnx"};
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
