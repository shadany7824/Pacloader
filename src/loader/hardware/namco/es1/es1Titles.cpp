#include "es1Title.h"
#include "maximumHeat3d/es1MaximumHeat3D.hpp"
#include "wmmt4/es1Wmmt4.h"

#include "../../../log/log.h"

#include <cstddef>
#include <cstring>

namespace
{
/* The titles this loader knows about; adding one means a module and a row here.
 * The first detect() to answer wins, so keep the cheap and specific tests ahead
 * of the ones that read package metadata. */
constexpr Es1Title Titles[] = {
    {
        "WMMT4",
        "WMMT4",
        "Wangan Midnight Maximum Tune 4",
        "WMN4r",
        "2011",
        JP,
        GROUP_WMMT4_ES1,
        es1Wmmt4Detect,
        es1Wmmt4InstallHooks,
        {
            {192, 168, 92, 11},
            {255, 255, 255, 0},
            /* The IC card reader owns /dev/ttyS1 here, so the shared kickback
             * device must not answer for it. */
            nullptr,
            1,
        },
    },
    {
        "MHEAT3D",
        "Maximum Heat 3D",
        "Maximum Heat 3D",
        "8807",
        "2009",
        US,
        GROUP_MAXIMUM_HEAT_3D,
        es1MaximumHeat3DDetect,
        es1MaximumHeat3DInstallHooks,
        {
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            "/dev/ttyS1",
            0,
        },
    },
};

constexpr Es1TitleQuirks NeutralQuirks = {{0, 0, 0, 0}, {0, 0, 0, 0}, nullptr, 0};

const Es1Title *g_current = nullptr;
} // namespace

extern "C" const Es1Title *es1CurrentTitle(void)
{
    return g_current;
}

extern "C" int es1TitleIs(const char *id)
{
    return g_current && id && std::strcmp(g_current->id, id) == 0 ? 1 : 0;
}

extern "C" const Es1TitleQuirks *es1TitleQuirks(void)
{
    return g_current ? &g_current->quirks : &NeutralQuirks;
}

extern "C" const Es1Title *es1SelectTitle(const char *elfPath)
{
    g_current = nullptr;
    if (!elfPath || !*elfPath)
        return nullptr;

    for (const Es1Title &title : Titles)
    {
        if (title.detect && title.detect(elfPath))
        {
            g_current = &title;
            log_info("Detected Namco System ES1 title: %s", title.title);
            return g_current;
        }
    }
    return nullptr;
}
