#include "configIni.h"

#include <stdio.h>

#include "config.h"
#include "../log/log.h"

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

    fprintf(file, "# pacloader configuration (Namco System N2 / ES1)\n\n");
    /* Platform selection is detected from the game package. */
    fprintf(file, "[Platform]\nSYSTEM = AUTO\n\n");

    fprintf(file, "[NamcoN2]\n");
    fprintf(file, "DONGLE_ID = \"%s\"\n", defaults.namcoN2.dongleId);
    fprintf(file, "DONGLE_ID_2 = \"%s\"\n", defaults.namcoN2.dongleId2);
    fprintf(file, "DEBUG_MODE = %s\n", defaults.namcoN2.debugMode ? "true" : "false");
    fprintf(file, "FFB_ENABLED = %s\n\n", defaults.namcoN2.forceFeedbackEnabled ? "true" : "false");
    fprintf(file, "FFB_DIAGNOSTICS = %s\n\n",
            defaults.namcoN2.forceFeedbackDiagnostics ? "true" : "false");

    /* These are read from [NamcoN2] and so have to be written here; they used
     * to sit under [NamcoES1], where nothing ever parsed them. */
    fprintf(file, "# YaCardEmu remains an external process.\n");
    fprintf(file, "YACARDEMU_ENABLED = %s\n", defaults.namcoN2.card.enabled ? "true" : "false");
    fprintf(file, "YACARDEMU_AUTOSTART = %s\n", defaults.namcoN2.card.autoStart ? "true" : "false");
    fprintf(file, "YACARDEMU_PATH = \"%s\"\n", defaults.namcoN2.card.executablePath);
    fprintf(file, "YACARDEMU_PIPE = \"%s\"\n", defaults.namcoN2.card.pipeName);
    fprintf(file, "YACARDEMU_API_HOST = \"%s\"\n", defaults.namcoN2.card.apiHost);
    fprintf(file, "YACARDEMU_API_PORT = %d\n", defaults.namcoN2.card.apiPort);
    fprintf(file, "YACARDEMU_CARD_NAME = \"%s\"\n", defaults.namcoN2.card.cardName);
    fprintf(file, "YACARDEMU_DIAGNOSTICS = %s\n\n",
            defaults.namcoN2.card.diagnostics ? "true" : "false");

    fprintf(file, "NETWORK_ENABLED = %s\n", defaults.namcoN2.network.enabled ? "true" : "false");
    fprintf(file, "NETWORK_INTERFACE = \"%s\"\n", defaults.namcoN2.network.interfaceName);
    fprintf(file, "NETWORK_BIND_ADDRESS = \"%s\"\n", defaults.namcoN2.network.bindAddress);
    fprintf(file, "NETWORK_BROADCAST_ADDRESS = \"%s\"\n",
            defaults.namcoN2.network.broadcastAddress);
    fprintf(file, "NETWORK_ALLOW_BROADCAST = %s\n",
            defaults.namcoN2.network.allowBroadcast ? "true" : "false");
    fprintf(file, "NETWORK_REWRITE_BROADCAST = %s\n\n",
            defaults.namcoN2.network.rewriteBroadcast ? "true" : "false");

    fprintf(file, "# Advanced cabinet calibration; defaults match the emulated N2 board.\n");
    fprintf(file, "STEERING_RAW_MIN = %d\nSTEERING_RAW_MAX = %d\n",
            defaults.namcoN2.steering.minimum, defaults.namcoN2.steering.maximum);
    fprintf(file, "ACCELERATOR_RAW_MIN = %d\nACCELERATOR_RAW_MAX = %d\n",
            defaults.namcoN2.accelerator.minimum, defaults.namcoN2.accelerator.maximum);
    fprintf(file, "BRAKE_RAW_MIN = %d\nBRAKE_RAW_MAX = %d\n\n",
            defaults.namcoN2.brake.minimum, defaults.namcoN2.brake.maximum);

    fprintf(file, "[NamcoES1]\n");
    fprintf(file, "CAMERA_ENABLED = %s\n", defaults.namcoES1.cameraEnabled ? "true" : "false");
    fprintf(file, "DONGLE_ENABLED = %s\n", defaults.namcoES1.dongleEnabled ? "true" : "false");
    fprintf(file, "SERIAL_DIAGNOSTICS = %s\n", defaults.namcoES1.serialDiagnostics ? "true" : "false");
    fprintf(file, "EMULATE_JAMMA = %s\n", defaults.namcoES1.emulateJamma ? "true" : "false");
    fprintf(file, "# DRIVE uses WMMT4 S/N 267610069420; TERMINAL uses 267611069420.\n");
    fprintf(file, "CABINET_MODE = DRIVE\n");
    fprintf(file, "# WMMT4 DRIVE only: supplies terminal settings and clock locally.\n");
    fprintf(file, "TERMINAL_EMULATOR_ENABLED = %s\n\n",
            defaults.namcoES1.terminalEmulatorEnabled ? "true" : "false");

    fprintf(file, "# Optional WMMT4 hostname redirects; blank uses normal DNS.\n");
    fprintf(file, "DNS_NBGI_LOC = \"%s\"\n", defaults.namcoES1.dnsNbgiLoc);
    fprintf(file, "DNS_TENPOROUTER_LOC = \"%s\"\n", defaults.namcoES1.dnsTenporouterLoc);
    fprintf(file, "DNS_BBROUTER_LOC = \"%s\"\n", defaults.namcoES1.dnsBbrouterLoc);
    fprintf(file, "DNS_MUCHA_LOCAL = \"%s\"\n", defaults.namcoES1.dnsMuchaLocal);
    fprintf(file, "DNS_NAOMINET_JP = \"%s\"\n\n", defaults.namcoES1.dnsNaominetJp);

    fprintf(file, "# Magnetic card reader, for transferring WMMT3DX+ cards.\n");
    fprintf(file, "# TERMINAL cabinets only; the drive cabinet has no reader.\n");
    fprintf(file, "YACARDEMU_ENABLED = %s\n", defaults.namcoES1.card.enabled ? "true" : "false");
    fprintf(file, "YACARDEMU_AUTOSTART = %s\n", defaults.namcoES1.card.autoStart ? "true" : "false");
    fprintf(file, "YACARDEMU_PATH = \"%s\"\n", defaults.namcoES1.card.executablePath);
    fprintf(file, "YACARDEMU_PIPE = \"%s\"\n", defaults.namcoES1.card.pipeName);
    fprintf(file, "YACARDEMU_API_HOST = \"%s\"\n", defaults.namcoES1.card.apiHost);
    fprintf(file, "YACARDEMU_API_PORT = %d\n", defaults.namcoES1.card.apiPort);
    fprintf(file, "YACARDEMU_CARD_NAME = \"%s\"\n", defaults.namcoES1.card.cardName);
    fprintf(file, "YACARDEMU_DIAGNOSTICS = %s\n\n",
            defaults.namcoES1.card.diagnostics ? "true" : "false");

    fprintf(file, "[Display]\nWIDTH = AUTO\nHEIGHT = AUTO\n");
    fprintf(file, "FULLSCREEN = %s\n", defaults.fullscreen ? "true" : "false");
    fprintf(file, "ALWAYS_ON_TOP = %s\n", defaults.alwaysOnTop ? "true" : "false");
    fprintf(file, "KEEP_ASPECT_RATIO = %s\n", defaults.keepAspectRatio ? "true" : "false");
    fprintf(file, "HIDE_CURSOR = %s\n\n", defaults.hideCursor ? "true" : "false");

    fprintf(file, "[Emulation]\nREGION = JP\nFREEPLAY = none\nEMULATE_JVS = true\n\n");
    fprintf(file, "[Graphics]\nFPS_LIMITER_ENABLED = %s\nFPS_TARGET = %.1f\n\n",
            defaults.fpsLimiter ? "true" : "false", defaults.fpsTarget);
    fprintf(file, "[System]\nDEBUG_MSGS = %s\n",
            defaults.showDebugMessages ? "true" : "false");

    fclose(file);
    printf("Created pacloader configuration: %s\n", filePath);
    return 1;
}
