#include "n2Title.h"

#if defined(_WIN32) || defined(__MINGW32__)

#include "csneo/n2CsNeo.h"
#include "wmmt3/n2Wmmt3.hpp"

#include "../../../log/log.h"

#include <cstring>
#include <string>

namespace
{
/*
 * The titles this loader knows about; adding one means a module and a row here.
 * The first detect() to answer wins, so the specific Wangan revisions come
 * before the family row that accepts any other gRomInfo.
 */
const N2Title Titles[] = {
    {
        N2_TITLE_ID_WMMT3, "WMMT", "WMMT3", "Wangan Midnight Maximum Tune 3", "", "640x480",
        GROUP_WMMT3, DRIVING, 640, 480, n2Wmmt3Detect, n2Wmmt3InstallHooks, n2Wmmt3HandleSystemCommand,
        nullptr, n2Wmmt3ShouldBlit, {1, 1, 1, 1, CABINET_PANEL_WANGAN_N2},
    },
    {
        N2_TITLE_ID_WMMT3DX_PLUS, "WMMT", "WMMT3DX+", "Wangan Midnight Maximum Tune 3DX+", "2010", "640x480",
        GROUP_WMMT3, DRIVING, 640, 480, n2Wmmt3dxPlusDetect, n2Wmmt3InstallHooks, n2Wmmt3HandleSystemCommand,
        nullptr, n2Wmmt3ShouldBlit, {1, 1, 1, 1, CABINET_PANEL_WANGAN_N2},
    },
    {
        N2_TITLE_ID_WMMT3DX, "WMMT", "WMMT3DX", "Wangan Midnight Maximum Tune 3DX", "", "640x480",
        GROUP_WMMT3, DRIVING, 640, 480, n2Wmmt3dxDetect, n2Wmmt3InstallHooks, n2Wmmt3HandleSystemCommand,
        nullptr, n2Wmmt3ShouldBlit, {1, 1, 1, 1, CABINET_PANEL_WANGAN_N2},
    },
    {
        N2_TITLE_ID_WMMT3_FAMILY, "WMMT", "WMMT3 series", "Wangan Midnight Maximum Tune 3 series", "", "640x480",
        GROUP_WMMT3, DRIVING, 640, 480, n2Wmmt3FamilyDetect, n2Wmmt3InstallHooks, n2Wmmt3HandleSystemCommand,
        nullptr, n2Wmmt3ShouldBlit, {1, 1, 1, 1, CABINET_PANEL_WANGAN_N2},
    },
    {
        N2_TITLE_ID_CSNEO, nullptr, "CSNeo", "Counter-Strike Neo", "2005", "1024x768",
        GROUP_UNKNOWN, SHOOTING, 1024, 768, n2CsNeoDetect, n2CsNeoInstallHooks, nullptr,
        n2CsNeoHandleHostKey, nullptr, {0, 0, 0, 0, CABINET_PANEL_GENERIC},
    },
};

constexpr N2TitleQuirks NeutralQuirks = {0, 0, 0, 0, CABINET_PANEL_GENERIC};

const N2Title *g_current = nullptr;
std::string g_revision;
} // namespace

extern "C" const N2Title *n2CurrentTitle(void)
{
    return g_current;
}

extern "C" const N2TitleQuirks *n2TitleQuirks(void)
{
    return g_current ? &g_current->quirks : &NeutralQuirks;
}

extern "C" int n2TitleIs(const char *id)
{
    return g_current && id && std::strcmp(g_current->id, id) == 0 ? 1 : 0;
}

extern "C" void n2SetDetectedRevision(const char *revision)
{
    g_revision.assign(revision ? revision : "");
}

extern "C" const char *n2DetectedRevision(void)
{
    return g_revision.c_str();
}

extern "C" const N2Title *n2SelectTitle(const char *elfPath)
{
    g_current = nullptr;
    g_revision.clear();

    for (const N2Title &title : Titles)
    {
        if (!title.detect || !title.detect(elfPath))
            continue;

        g_current = &title;
        if (g_revision.empty())
            log_info("Detected Namco System N2 title: %s", title.title);
        else
            log_info("Detected Namco System N2 title: %s, revision %s", title.title,
                     g_revision.c_str());
        return g_current;
    }
    return nullptr;
}

#endif
