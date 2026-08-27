#include "es1Title.h"
#include "kizuna/es1Kizuna.hpp"
#include "maximumHeat3d/es1MaximumHeat3D.hpp"
#include "wmmt4/es1Wmmt4.h"
#include "wmmt5/es1Wmmt5.h"

#include "../../../log/log.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace
{
/* The titles this loader knows about; adding one means a module and a row here.
 * The first detect() to answer wins, so keep the cheap and specific tests ahead
 * of the ones that read package metadata. */
constexpr Es1Title Titles[] = {
    {
        "WMMT4",
        "WMMT4",
        "WMMT4",
        "Wangan Midnight Maximum Tune 4",
        "WMN4r",
        "2011",
        1360,
        768,
        "1360x768",
        JP,
        GROUP_WMMT4_ES1,
        es1Wmmt4Detect,
        es1Wmmt4InstallHooks,
        {
            {192, 168, 92, 11},
            {255, 255, 255, 0},
            /* The IC card reader owns /dev/ttyS1 here; the steering PCB is on
             * /dev/ttyS0 at 19200, which the title calls "Str2". */
            "/dev/ttyS0",
            1,
            /* Fixed cabinet panel, and an H-pattern shifter on the JVS bits. */
            1,
            1,
            /* Card is a reader command, TEST latches, the axis is polled, and
             * the guest is told it runs windowed. Pedals are reported shifted. */
            1,
            1,
            1,
            1,
            320 << 6,
            CABINET_PANEL_WANGAN_ES1,
            /* The steering board volunteers its state, self-check included. */
            1,
            1,
            /* NA-JV identity from the WMMT5DX+ manual's I/O TEST screen. It does not
             * redefine the feature counts, so keep WMMT4's capability profile. */
            "namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose.",
            /* Keep the generic ES1 capability profile until WMMT4's own
             * feature enumeration is verified against a real cabinet. */
            {0},
        },
    },
    {
        /* TODO: provisional. Most traits are WMMT4's and unconfirmed here. */
        "WMMT5DX+",
        "WMMT4",
        "WMMT5DX+",
        "Wangan Midnight Maximum Tune 5DX+",
        "WMN5r",
        "2015",
        1360,
        768,
        "1360x768",
        EX,
        GROUP_WMMT4_ES1,
        es1Wmmt5dxPlusDetect,
        es1Wmmt5dxPlusInstallHooks,
        {
            {192, 168, 92, 11},
            {255, 255, 255, 0},
            "/dev/ttyS0",
            /* TODO: unverified against this build. */
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            320 << 6,
            CABINET_PANEL_WANGAN_ES1,
            1,
            /* Its self-check result rides the power-on reply instead. */
            0,
            /* This build's JVIO master wants a Ver4.00 board; Ver1.00 is WMMT4's. */
            "namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose",
            /* declared, players, switches, coins, analogue in channels/bits,
             * rotary, keypad, general out, analogue out, display columns/rows. */
            {1, 1, 24, 2, 8, 16, 4, 0, 18, 2, 80, 25},
        },
    },
    {
        /* TODO: provisional.  Everything below the identification is a guess
         * from the executable's strings and has not been seen running. */
        "KIZUNA3",
        "Kizuna",
        "Kizuna",
        "Kidou Senshi Gundam: Senjou no Kizuna",
        "GKE1",
        "2016",
        1024,
        768,
        "1024x768",
        JP,
        GROUP_KIZUNA_ES1,
        es1KizunaDetect,
        es1KizunaInstallHooks,
        {
            /* No fixed address known; report whatever the host adapter has. */
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            /* No steering board - the POD has sticks and pedals over JVS. */
            nullptr,
            /* TODO: unverified against this build. */
            0,
            /* The POD's screen is a fixed projector panel. */
            1,
            /* No H-pattern shifter, and the card is not a reader command. */
            0,
            0,
            /* Its TEST switch latches, the same as the Wangan cabinets - without
             * this the key has to be held down for test mode to stay up. */
            1,
            0,
            0,
            0,
            CABINET_PANEL_KIZUNA_POD,
            0,
            0,
            /* Same JVIO family as WMMT4; the executable carries NA-JV beside its
             * /dev/ttyS2 open. The version digits are a guess. */
            "namco ltd.;NA-JV;Ver1.00;JPN,Multipurpose",
            {0},
        },
    },
    {
        "MHEAT3D",
        "MaximumHeat3D",
        "Maximum Heat 3D",
        "Maximum Heat 3D",
        "8807",
        "2009",
        1360,
        768,
        "1360x768",
        US,
        GROUP_MAXIMUM_HEAT_3D,
        es1MaximumHeat3DDetect,
        es1MaximumHeat3DInstallHooks,
        {
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            "/dev/ttyS1",
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            CABINET_PANEL_MAXIMUM_HEAT_3D,
            0,
            0,
            nullptr,
            {0},
        },
    },
};

constexpr Es1TitleQuirks NeutralQuirks = {{0, 0, 0, 0}, {0, 0, 0, 0}, nullptr, 0,
                                          0, 0, 0, 0, 0, 0, 0,
                                          CABINET_PANEL_GENERIC, 0, 0, nullptr, {0}};

const Es1Title *g_current = nullptr;
std::string g_revision;
} // namespace

extern "C" void es1SetDetectedRevision(const char *revision)
{
    g_revision.assign(revision ? revision : "");
}

extern "C" const char *es1DetectedRevision(void)
{
    return g_revision.c_str();
}

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
    g_revision.clear();
    if (!elfPath || !*elfPath)
        return nullptr;

    for (const Es1Title &title : Titles)
    {
        if (title.detect && title.detect(elfPath))
        {
            g_current = &title;
            if (g_revision.empty())
                log_info("Detected Namco System ES1 title: %s", title.title);
            else
                log_info("Detected Namco System ES1 title: %s, revision %s", title.title,
                         g_revision.c_str());
            return g_current;
        }
    }
    return nullptr;
}
