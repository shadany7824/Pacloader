#include "configIni.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__)
#include <io.h>
#define pathExists(p) (_access((p), 0) == 0)
#else
#include <unistd.h>
#define pathExists(p) (access((p), F_OK) == 0)
#endif

#include "config.h"
#include "../log/log.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/es1Title.h"
#include "../hardware/namco/n2/n2Title.h"
#include "../platform/platformBackend.h"

#ifndef MAX_PATH_LENGTH
#define MAX_PATH_LENGTH 1024
#endif

extern char *games[];

typedef enum
{
    IniTargetUnknown,
    IniTargetN2,
    IniTargetES1
} IniTarget;

/*
 * The file is written into the game's own folder, so the board can be named by
 * finding the game's ELF beside it. Without that every board's settings were
 * written for every game, which is most of what made the result unreadable.
 *
 */
static IniTarget detectTarget(const char *filePath, const char **gameName,
                             const char **revision)
{
    char directory[MAX_PATH_LENGTH];
    snprintf(directory, sizeof(directory), "%s", filePath);
    char *cut = strrchr(directory, '/');
    char *backslash = strrchr(directory, '\\');
    if (backslash && (!cut || backslash > cut))
        cut = backslash;
    if (!cut)
        return IniTargetUnknown;
    *cut = '\0';

    for (int i = 0; games[i] && strcmp(games[i], "END") != 0; i++)
    {
        char candidate[MAX_PATH_LENGTH];
        snprintf(candidate, sizeof(candidate), "%s/%s", directory, games[i]);
        if (!pathExists(candidate))
            continue;
        if (!platformDetectGame(candidate))
            continue;

        if (platformIsES1())
        {
            const Es1Title *title = es1CurrentTitle();
            if (title)
                *gameName = title->title;
            *revision = es1DetectedRevision();
            return IniTargetES1;
        }
        if (platformIsN2())
        {
            const N2Title *title = n2CurrentTitle();
            if (title)
                *gameName = title->title;
            *revision = n2DetectedRevision();
            return IniTargetN2;
        }
    }
    return IniTargetUnknown;
}

static void writeNamcoN2(FILE *file, const EmulatorConfig *defaults)
{
    fprintf(file, "[NamcoN2]\n");
    fprintf(file, "DONGLE_ID = \"%s\"\n", defaults->namcoN2.dongleId);
    fprintf(file, "DONGLE_ID_2 = \"%s\"\n", defaults->namcoN2.dongleId2);
    fprintf(file, "DEBUG_MODE = %s\n", defaults->namcoN2.debugMode ? "true" : "false");
    fprintf(file, "FFB_ENABLED = %s\n", defaults->namcoN2.forceFeedbackEnabled ? "true" : "false");
    fprintf(file, "FFB_DIAGNOSTICS = %s\n\n",
            defaults->namcoN2.forceFeedbackDiagnostics ? "true" : "false");

    fprintf(file, "# YaCardEmu remains an external process.\n");
    fprintf(file, "YACARDEMU_ENABLED = %s\n", defaults->namcoN2.card.enabled ? "true" : "false");
    fprintf(file, "YACARDEMU_AUTOSTART = %s\n", defaults->namcoN2.card.autoStart ? "true" : "false");
    fprintf(file, "YACARDEMU_PATH = \"%s\"\n", defaults->namcoN2.card.executablePath);
    fprintf(file, "YACARDEMU_PIPE = \"%s\"\n", defaults->namcoN2.card.pipeName);
    fprintf(file, "YACARDEMU_API_HOST = \"%s\"\n", defaults->namcoN2.card.apiHost);
    fprintf(file, "YACARDEMU_API_PORT = %d\n", defaults->namcoN2.card.apiPort);
    fprintf(file, "YACARDEMU_CARD_NAME = \"%s\"\n", defaults->namcoN2.card.cardName);
    fprintf(file, "YACARDEMU_DIAGNOSTICS = %s\n\n",
            defaults->namcoN2.card.diagnostics ? "true" : "false");

    fprintf(file, "NETWORK_ENABLED = %s\n", defaults->namcoN2.network.enabled ? "true" : "false");
    fprintf(file, "NETWORK_INTERFACE = \"%s\"\n", defaults->namcoN2.network.interfaceName);
    fprintf(file, "NETWORK_BIND_ADDRESS = \"%s\"\n", defaults->namcoN2.network.bindAddress);
    fprintf(file, "NETWORK_BROADCAST_ADDRESS = \"%s\"\n",
            defaults->namcoN2.network.broadcastAddress);
    fprintf(file, "NETWORK_ALLOW_BROADCAST = %s\n",
            defaults->namcoN2.network.allowBroadcast ? "true" : "false");
    fprintf(file, "NETWORK_REWRITE_BROADCAST = %s\n\n",
            defaults->namcoN2.network.rewriteBroadcast ? "true" : "false");

    fprintf(file, "# Advanced cabinet calibration; defaults match the emulated N2 board.\n");
    fprintf(file, "STEERING_RAW_MIN = %d\nSTEERING_RAW_MAX = %d\n",
            defaults->namcoN2.steering.minimum, defaults->namcoN2.steering.maximum);
    fprintf(file, "ACCELERATOR_RAW_MIN = %d\nACCELERATOR_RAW_MAX = %d\n",
            defaults->namcoN2.accelerator.minimum, defaults->namcoN2.accelerator.maximum);
    fprintf(file, "BRAKE_RAW_MIN = %d\nBRAKE_RAW_MAX = %d\n\n",
            defaults->namcoN2.brake.minimum, defaults->namcoN2.brake.maximum);
}

static void writeNamcoES1(FILE *file, const EmulatorConfig *defaults)
{
    fprintf(file, "[NamcoES1]\n");
    fprintf(file, "CAMERA_ENABLED = %s\n", defaults->namcoES1.cameraEnabled ? "true" : "false");
    fprintf(file, "DONGLE_ENABLED = %s\n", defaults->namcoES1.dongleEnabled ? "true" : "false");
    fprintf(file, "SERIAL_DIAGNOSTICS = %s\n", defaults->namcoES1.serialDiagnostics ? "true" : "false");
    fprintf(file, "EMULATE_JAMMA = %s\n", defaults->namcoES1.emulateJamma ? "true" : "false");
    fprintf(file, "# DRIVE uses WMMT4 S/N 267610069420; TERMINAL uses 267611069420.\n");
    fprintf(file, "CABINET_MODE = DRIVE\n");
    fprintf(file, "# WMMT4 DRIVE only: supplies terminal settings and clock locally.\n");
    fprintf(file, "TERMINAL_EMULATOR_ENABLED = %s\n\n",
            defaults->namcoES1.terminalEmulatorEnabled ? "true" : "false");

    fprintf(file, "FORCE_FEEDBACK_ENABLED = %s\n",
            defaults->namcoES1.forceFeedbackEnabled ? "true" : "false");
    fprintf(file, "FORCE_FEEDBACK_DIAGNOSTICS = %s\n",
            defaults->namcoES1.forceFeedbackDiagnostics ? "true" : "false");
    fprintf(file, "FFB_GAIN = %d\nFFB_WEIGHT = %d\n",
            defaults->namcoES1.ffbGain, defaults->namcoES1.ffbWeight);
    fprintf(file, "FFB_SPRING = %d\nFFB_DAMPER = %d\n",
            defaults->namcoES1.ffbSpringGain, defaults->namcoES1.ffbDamperGain);
    fprintf(file, "FFB_VIBRATION = %d\nFFB_BASE_DAMPER = %d\n",
            defaults->namcoES1.ffbVibrationGain, defaults->namcoES1.ffbBaseDamper);
    fprintf(file, "FFB_DEADBAND = %d\nFFB_RUMBLE_DURATION = %d\n",
            defaults->namcoES1.ffbDeadband, defaults->namcoES1.ffbRumbleDuration);
    fprintf(file, "FFB_INVERT = %s\n\n", defaults->namcoES1.ffbInvert ? "true" : "false");

    fprintf(file, "# Optional WMMT4 hostname redirects; blank uses normal DNS.\n");
    fprintf(file, "DNS_NBGI_LOC = \"%s\"\n", defaults->namcoES1.dnsNbgiLoc);
    fprintf(file, "DNS_TENPOROUTER_LOC = \"%s\"\n", defaults->namcoES1.dnsTenporouterLoc);
    fprintf(file, "DNS_BBROUTER_LOC = \"%s\"\n", defaults->namcoES1.dnsBbrouterLoc);
    fprintf(file, "DNS_MUCHA_LOCAL = \"%s\"\n", defaults->namcoES1.dnsMuchaLocal);
    fprintf(file, "DNS_NAOMINET_JP = \"%s\"\n\n", defaults->namcoES1.dnsNaominetJp);

    fprintf(file, "IC_CARD_ENABLED = %s\n", defaults->namcoES1.icCard.enabled ? "true" : "false");
    fprintf(file, "IC_CARD_AUTO_INSERT = %s\n",
            defaults->namcoES1.icCard.autoInsert ? "true" : "false");
    fprintf(file, "IC_CARD_DIAGNOSTICS = %s\n",
            defaults->namcoES1.icCard.diagnostics ? "true" : "false");
    fprintf(file, "IC_CARD_FILE = \"%s\"\n\n", defaults->namcoES1.icCard.cardFile);

    fprintf(file, "# WMMT3DX+ magnetic card dump used by the terminal reader.\n");
    fprintf(file, "LEGACY_CARD_ENABLED = %s\n",
            defaults->namcoES1.legacyCard.enabled ? "true" : "false");
    fprintf(file, "LEGACY_CARD_AUTO_INSERT = %s\n",
            defaults->namcoES1.legacyCard.autoInsert ? "true" : "false");
    fprintf(file, "LEGACY_CARD_DIAGNOSTICS = %s\n",
            defaults->namcoES1.legacyCard.diagnostics ? "true" : "false");
    fprintf(file, "LEGACY_CARD_FILE = \"%s\"\n\n", defaults->namcoES1.legacyCard.cardFile);

    fprintf(file, "# Magnetic card reader, for transferring WMMT3DX+ cards.\n");
    fprintf(file, "# TERMINAL cabinets only; the drive cabinet has no reader.\n");
    fprintf(file, "YACARDEMU_ENABLED = %s\n", defaults->namcoES1.card.enabled ? "true" : "false");
    fprintf(file, "YACARDEMU_AUTOSTART = %s\n", defaults->namcoES1.card.autoStart ? "true" : "false");
    fprintf(file, "YACARDEMU_PATH = \"%s\"\n", defaults->namcoES1.card.executablePath);
    fprintf(file, "YACARDEMU_PIPE = \"%s\"\n", defaults->namcoES1.card.pipeName);
    fprintf(file, "YACARDEMU_API_HOST = \"%s\"\n", defaults->namcoES1.card.apiHost);
    fprintf(file, "YACARDEMU_API_PORT = %d\n", defaults->namcoES1.card.apiPort);
    fprintf(file, "YACARDEMU_CARD_NAME = \"%s\"\n", defaults->namcoES1.card.cardName);
    fprintf(file, "YACARDEMU_DIAGNOSTICS = %s\n\n",
            defaults->namcoES1.card.diagnostics ? "true" : "false");
}

int createDefaultIni(const char *filePath)
{
    FILE *file = fopen(filePath, "w");
    if (!file)
    {
        log_error("Could not create default INI file at %s", filePath);
        return -1;
    }

    EmulatorConfig defaults = {0};
    setDefaultValues(&defaults);

    const char *gameName = NULL;
    const char *revision = NULL;
    const IniTarget target = detectTarget(filePath, &gameName, &revision);
    const char *boardName = target == IniTargetES1   ? "Namco System ES1"
                            : target == IniTargetN2  ? "Namco System N2"
                                                     : "Namco System N2 / ES1";

    if (gameName)
    {
        fprintf(file, "# pacloader configuration for %s (%s)\n", gameName, boardName);
        if (revision && *revision)
            fprintf(file, "# package revision %s\n", revision);
        fprintf(file, "\n");
    }
    else
    {
        fprintf(file, "# pacloader configuration (%s)\n", boardName);
        fprintf(file, "# No game was recognised beside this file, so both boards are listed.\n\n");
    }
    fprintf(file, "[Platform]\nSYSTEM = AUTO\n\n");

    if (target != IniTargetES1)
        writeNamcoN2(file, &defaults);
    if (target != IniTargetN2)
        writeNamcoES1(file, &defaults);

    fprintf(file, "[Display]\nWIDTH = AUTO\nHEIGHT = AUTO\n");
    fprintf(file, "FULLSCREEN = %s\n", defaults.fullscreen ? "true" : "false");
    fprintf(file, "KEEP_ASPECT_RATIO = %s\n", defaults.keepAspectRatio ? "true" : "false");
    fprintf(file, "HIDE_CURSOR = %s\n\n", defaults.hideCursor ? "true" : "false");

    fprintf(file, "[Emulation]\nREGION = JP\nFREEPLAY = none\nEMULATE_JVS = true\nEMULATE_TOUCHSCREEN = false\n\n");
    fprintf(file, "[Graphics]\nFPS_LIMITER_ENABLED = %s\nFPS_TARGET = %.1f\n",
            defaults.fpsLimiter ? "true" : "false", defaults.fpsTarget);
    fprintf(file, "# true: present on the display's refresh (no tearing).\n");
    fprintf(file, "VSYNC = %s\n\n", defaults.vsync ? "true" : "false");
    fprintf(file, "[System]\nDEBUG_MSGS = %s\n",
            defaults.showDebugMessages ? "true" : "false");

    fclose(file);
    printf("Created pacloader configuration for %s: %s\n",
           gameName ? gameName : boardName, filePath);
    return 1;
}
