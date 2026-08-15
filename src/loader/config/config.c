#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "../graphics/gpuVendor.h"
#include "../log/log.h"
#include "iniParser.h"
#include "../mainShared.h"
#if defined(_WIN32) || defined(__MINGW32__)
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/es1Title.h"
#include "../hardware/namco/n2/n2.h"
#include "../hardware/namco/n2/n2Title.h"
#endif

EmulatorConfig config = {0};

extern uint32_t partialElfCrc;

FILE *configFile = NULL;

#define CONFIG_PATH "linuxloader.ini"
#define MAX_LINE_LENGTH 1024

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *GameRegionStrings[] = {"Japan", "US", "Export"};

const char *GpuTypeStrings[] = {"Auto Detection", "NVIDIA", "AMD", "ATI", "INTEL", "Unknown", "ERROR_GPU"};

static int detectGame(uint32_t elf_crc)
{
#if defined(_WIN32) || defined(__MINGW32__)
    if (es1IsDetected())
    {
        config.platform = ARCADE_PLATFORM_NAMCO_ES1;
        config.gameTitle = (char *)es1GetGameTitle();
        config.gameShortTitle = (char *)es1GetGameShortTitle();
        config.gameDVP = (char *)"Namco System ES1";
        config.gameID = (char *)es1GetGameId();
        config.gameStatus = NOT_WORKING;
        config.jvsIOType = NAMCO_ES1_TYPE;
        config.region = es1CurrentTitle()->region;
        config.gameReleaseYear = (char *)es1CurrentTitle()->releaseYear;
        config.gameNativeResolutions = (char *)"1360x768";
        config.gameType = DRIVING;
        config.width = 1360;
        config.height = 768;
        config.gameGroup = es1CurrentTitle()->group;
        log_warn("System ES1 support is experimental: cabinet I/O is emulated by Pacloader");
        return 0;
    }

    if (n2IsDetected())
    {
        config.platform = ARCADE_PLATFORM_NAMCO_N2;
        config.gameTitle = (char *)n2GetGameTitle();
        config.gameShortTitle = (char *)n2GetGameShortTitle();
        config.gameDVP = (char *)"Namco System N2";
        config.gameID = (char *)n2GetGameId();
        config.gameStatus = WORKING;
        config.jvsIOType = NAMCO_N2_TYPE;
        config.region = JP;
        config.gameReleaseYear = (char *)n2CurrentTitle()->releaseYear;
        config.gameNativeResolutions = (char *)n2CurrentTitle()->nativeResolutions;
        config.gameType = n2CurrentTitle()->type;
        config.width = n2CurrentTitle()->width;
        config.height = n2CurrentTitle()->height;
        config.gameGroup = n2CurrentTitle()->group;
        return 0;
    }
#endif
    log_error("Unsupported ELF: pacloader did not detect a supported Namco System N2 or ES1 title");
    config.crc32 = UNKNOWN;
    return 1;
}

char *getGameName()
{
    return config.gameTitle;
}

char *getDvpName()
{
    return config.gameDVP;
}

char *getGameId()
{
    return config.gameID;
}

char *getGameReleaseYear()
{
    return config.gameReleaseYear;
}

char *getGameNativeResolutions()
{
    return config.gameNativeResolutions;
}

const char *getGameRegionString(GameRegion region)
{
    return GameRegionStrings[region];
}

const char *getGpuTypeString(GpuType gpuType)
{
    return GpuTypeStrings[gpuType];
}

void toLowerCase(char *str)
{
    while (*str)
    {
        *str = tolower((unsigned char)*str);
        str++;
    }
}

void setDefaultValues(EmulatorConfig *cfg)
{
    cfg->platform = ARCADE_PLATFORM_NAMCO_N2;
    cfg->namcoES1.cameraEnabled = 1;
    cfg->namcoES1.dongleEnabled = 1;
    cfg->namcoES1.serialDiagnostics = 0;
    cfg->namcoES1.emulateJamma = 1;
    cfg->namcoES1.cabinetMode = NAMCO_ES1_CABINET_DRIVE;
    cfg->namcoES1.terminalEmulatorEnabled = 1;
    strcpy(cfg->namcoES1.dnsNbgiLoc, "");
    strcpy(cfg->namcoES1.dnsTenporouterLoc, "");
    strcpy(cfg->namcoES1.dnsBbrouterLoc, "");
    strcpy(cfg->namcoES1.dnsMuchaLocal, "");
    strcpy(cfg->namcoES1.dnsNaominetJp, "");
    cfg->namcoES1.icCard.enabled = 1;
    cfg->namcoES1.icCard.autoInsert = 1;
    cfg->namcoES1.icCard.diagnostics = 0;
    strcpy(cfg->namcoES1.icCard.cardFile, "wmmt4-card.ini");
    cfg->namcoES1.legacyCard.enabled = 0;
    cfg->namcoES1.legacyCard.autoInsert = 1;
    cfg->namcoES1.legacyCard.diagnostics = 0;
    strcpy(cfg->namcoES1.legacyCard.cardFile, "wmmt3dx-card.bin");
    cfg->namcoES1.forceFeedbackEnabled = 1;
    cfg->namcoES1.forceFeedbackDiagnostics = 0;
    cfg->namcoES1.ffbGain = 100;
    cfg->namcoES1.ffbWeight = 100;
    cfg->namcoES1.ffbSpringGain = 100;
    cfg->namcoES1.ffbDamperGain = 100;
    cfg->namcoES1.ffbVibrationGain = 100;
    cfg->namcoES1.ffbBaseDamper = 0;
    cfg->namcoES1.ffbDeadband = 0;
    cfg->namcoES1.ffbRumbleDuration = 100;
    cfg->namcoES1.ffbInvert = 0;
    cfg->namcoES1.card.enabled = 1;
    cfg->namcoES1.card.autoStart = 0;
    cfg->namcoES1.card.diagnostics = 0;
    cfg->namcoES1.card.apiPort = 8080;
    strcpy(cfg->namcoES1.card.executablePath, "");
    strcpy(cfg->namcoES1.card.pipeName, "\\\\.\\pipe\\YACardEmu");
    strcpy(cfg->namcoES1.card.apiHost, "127.0.0.1");
    strcpy(cfg->namcoES1.card.cardName, "");
    strcpy(cfg->namcoN2.dongleId, "");
    strcpy(cfg->namcoN2.dongleId2, "");
    cfg->namcoN2.debugMode = 0;
    cfg->namcoN2.forceFeedbackEnabled = 0;
    cfg->namcoN2.forceFeedbackDiagnostics = 0;
    cfg->namcoN2.ffbGain = 100;
    cfg->namcoN2.ffbSpringGain = 100;
    cfg->namcoN2.ffbDamperGain = 100;
    cfg->namcoN2.ffbVibrationGain = 100;
    // Reflection was measured swinging about thirty five counts of its signed
    // byte during a race, so this is where full strength lands rather than 127.
    cfg->namcoN2.ffbReflectRange = 40;
    cfg->namcoN2.ffbInvert = 0;
    cfg->namcoN2.ffbDamperFloor = 0;
    cfg->namcoN2.ffbSpringDeadband = 0;
    // The wheel swings its whole electrical range; the pedals stay inside the
    // window clInputDeviceJamma calibrates them in.
    cfg->namcoN2.steering.minimum = 0;
    cfg->namcoN2.steering.maximum = 65535;
    cfg->namcoN2.accelerator.minimum = 35000;
    cfg->namcoN2.accelerator.maximum = 55480;
    cfg->namcoN2.brake.minimum = 29000;
    cfg->namcoN2.brake.maximum = 49480;
    strcpy(cfg->namcoN2.jvs.name, "namco ltd.;JYU-PCB;Ver1.00;JPN,Multipurpose");
    cfg->namcoN2.jvs.players = 2;
    cfg->namcoN2.jvs.switches = 24;
    cfg->namcoN2.jvs.coins = 2;
    cfg->namcoN2.jvs.analogueInputs = 8;
    cfg->namcoN2.jvs.generalPurposeOutputs = 6;
    cfg->namcoN2.jvs.analogueOutputs = 4;
    cfg->namcoN2.jvs.generalPurposeInputs = 16;
    // The cabinet reports E51 whenever the reader does not answer, so the
    // loader tries the YaCardEmu pipe by default.
    cfg->namcoN2.card.enabled = 1;
    cfg->namcoN2.card.autoStart = 0;
    cfg->namcoN2.card.diagnostics = 0;
    cfg->namcoN2.card.apiPort = 8080;
    strcpy(cfg->namcoN2.card.executablePath, "");
    strcpy(cfg->namcoN2.card.pipeName, "\\\\.\\pipe\\YACardEmu");
    strcpy(cfg->namcoN2.card.apiHost, "127.0.0.1");
    strcpy(cfg->namcoN2.card.cardName, "");
    cfg->namcoN2.network.enabled = 1;
    strcpy(cfg->namcoN2.network.interfaceName, "");
    strcpy(cfg->namcoN2.network.bindAddress, "");
    strcpy(cfg->namcoN2.network.broadcastAddress, "");
    cfg->namcoN2.network.allowBroadcast = 1;
    cfg->namcoN2.network.rewriteBroadcast = 0;
    cfg->emulateRideboard = 0;
    cfg->emulateDriveboard = 0;
    cfg->emulateMotionboard = 0;
    cfg->emulateHW210CardReader = 0;
    cfg->emulateIDCardReader = 0;
    cfg->emulateTouchscreen = 0;
    cfg->emulateJVS = 1;
    cfg->fullscreen = 0;
    cfg->width = 640;
    cfg->height = 480;
    cfg->boostRenderRes = 1;
    cfg->region = EX;
    cfg->freeplay = -1;
    cfg->showDebugMessages = 0;
    cfg->useAltJvsPassthrough = 0;
    cfg->hummerFlickerFix = 0;
    cfg->keepAspectRatio = 1;
    cfg->idSteeringPercentageReduction = 0.0f;
    cfg->lgjRenderWithMesa = 1;
    cfg->gameTitle = "Unknown game";
    cfg->gameID = "XXXX";
    cfg->gameDVP = "DVP-XXXX";
    cfg->gameType = SHOOTING;
    cfg->gameReleaseYear = "";
    cfg->gameNativeResolutions = "";
    cfg->jvsIOType = NAMCO_N2_TYPE;
    cfg->GPUVendor = AUTO_DETECT_GPU;
    cfg->fpsLimiter = 1;
    cfg->fpsTarget = 60.0f;
    cfg->vsync = 0;
    cfg->disableBuiltinFont = 0;
    cfg->disableBuiltinLogos = 0;
    cfg->hideCursor = 1;
    cfg->mj4EnabledAtT = 0;
    cfg->crc32 = 0;
    cfg->gameGroup = GROUP_UNKNOWN;
    cfg->borderEnabled = 0;
    cfg->whiteBorderPercentage = 0.02f;
    cfg->blackBorderPercentage = 0.0f;
    cfg->inputMode = 1;
}

static const char *getValue(const IniConfig *ini, const char *sectionName, const char *key)
{
    IniSection *section = iniGetSection(ini, sectionName);
    if (!section)
    {
        return NULL;
    }
    for (int i = 0; i < section->numPairs; i++)
    {
        if (strcmp(section->pairs[i].key, key) == 0)
        {
            return section->pairs[i].value;
        }
    }
    return NULL;
}

static char *cleanValue(const char *rawValue, char *buffer, size_t bufferSize)
{
    if (!rawValue || bufferSize == 0)
        return NULL;

    strncpy(buffer, rawValue, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';

    char *start = buffer;
    char *end = buffer + strlen(buffer) - 1;

    while (isspace((unsigned char)*start))
        start++;
    while (end > start && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';

    if (*start == '"' && *end == '"')
    {
        start++;
        *end = '\0';
    }
    return start;
}

static int getInt(const IniConfig *ini, const char *section, const char *key, int defaultValue)
{
    const char *rawValue = getValue(ini, section, key);
    if (!rawValue)
        return defaultValue;

    char cleanBuffer[256];
    char *valueStr = cleanValue(rawValue, cleanBuffer, sizeof(cleanBuffer));
    if (!valueStr)
        return defaultValue;

    char tempStr[256];
    strncpy(tempStr, valueStr, sizeof(tempStr) - 1);
    tempStr[sizeof(tempStr) - 1] = '\0';
    toLowerCase(tempStr);

    if (strcmp(tempStr, "auto") == 0)
    {
        return defaultValue;
    }
    if (strcmp(tempStr, "true") == 0)
    {
        return 1;
    }
    if (strcmp(tempStr, "false") == 0)
    {
        return 0;
    }
    if (strcmp(tempStr, "none") == 0)
    {
        return -1;
    }
    return atoi(valueStr);
}

static float getFloat(const IniConfig *ini, const char *section, const char *key, float defaultValue)
{
    const char *rawValue = getValue(ini, section, key);
    if (!rawValue)
        return defaultValue;
    char cleanBuffer[256];
    char *valueStr = cleanValue(rawValue, cleanBuffer, sizeof(cleanBuffer));
    return valueStr ? atof(valueStr) : defaultValue;
}

static void getString(const IniConfig *ini, const char *section, const char *key, char *dest, int dest_size)
{
    const char *rawValue = getValue(ini, section, key);
    if (rawValue)
    {
        char cleanBuffer[MAX_PATH_LENGTH];
        char *valueStr = cleanValue(rawValue, cleanBuffer, sizeof(cleanBuffer));
        if (valueStr)
        {
            strncpy(dest, valueStr, dest_size - 1);
            dest[dest_size - 1] = '\0';
        }
    }
}

void applyIniConfig(EmulatorConfig *config, const IniConfig *ini)
{
    // [NamcoES1]
    config->namcoES1.cameraEnabled =
        getInt(ini, "NamcoES1", "CAMERA_ENABLED", config->namcoES1.cameraEnabled);
    config->namcoES1.dongleEnabled =
        getInt(ini, "NamcoES1", "DONGLE_ENABLED", config->namcoES1.dongleEnabled);
    config->namcoES1.serialDiagnostics =
        getInt(ini, "NamcoES1", "SERIAL_DIAGNOSTICS", config->namcoES1.serialDiagnostics);
    config->namcoES1.emulateJamma =
        getInt(ini, "NamcoES1", "EMULATE_JAMMA", config->namcoES1.emulateJamma);
    config->namcoES1.terminalEmulatorEnabled =
        getInt(ini, "NamcoES1", "TERMINAL_EMULATOR_ENABLED",
               config->namcoES1.terminalEmulatorEnabled);
    getString(ini, "NamcoES1", "DNS_NBGI_LOC", config->namcoES1.dnsNbgiLoc,
              sizeof(config->namcoES1.dnsNbgiLoc));
    getString(ini, "NamcoES1", "DNS_TENPOROUTER_LOC", config->namcoES1.dnsTenporouterLoc,
              sizeof(config->namcoES1.dnsTenporouterLoc));
    getString(ini, "NamcoES1", "DNS_BBROUTER_LOC", config->namcoES1.dnsBbrouterLoc,
              sizeof(config->namcoES1.dnsBbrouterLoc));
    getString(ini, "NamcoES1", "DNS_MUCHA_LOCAL", config->namcoES1.dnsMuchaLocal,
              sizeof(config->namcoES1.dnsMuchaLocal));
    getString(ini, "NamcoES1", "DNS_NAOMINET_JP", config->namcoES1.dnsNaominetJp,
              sizeof(config->namcoES1.dnsNaominetJp));
    {
        char cabinetMode[32];
        strcpy(cabinetMode, config->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL
                                ? "terminal"
                                : "drive");
        getString(ini, "NamcoES1", "CABINET_MODE", cabinetMode, sizeof(cabinetMode));
        toLowerCase(cabinetMode);
        if (strcmp(cabinetMode, "terminal") == 0)
            config->namcoES1.cabinetMode = NAMCO_ES1_CABINET_TERMINAL;
        else if (strcmp(cabinetMode, "drive") == 0)
            config->namcoES1.cabinetMode = NAMCO_ES1_CABINET_DRIVE;
        else
            log_warn("Unknown Namco ES1 CABINET_MODE '%s'; using DRIVE", cabinetMode);
    }
    config->namcoES1.forceFeedbackEnabled = getInt(
        ini, "NamcoES1", "FORCE_FEEDBACK_ENABLED", config->namcoES1.forceFeedbackEnabled);
    config->namcoES1.forceFeedbackDiagnostics =
        getInt(ini, "NamcoES1", "FORCE_FEEDBACK_DIAGNOSTICS",
               config->namcoES1.forceFeedbackDiagnostics);
    config->namcoES1.ffbGain =
        getInt(ini, "NamcoES1", "FFB_GAIN", config->namcoES1.ffbGain);
    config->namcoES1.ffbWeight =
        getInt(ini, "NamcoES1", "FFB_WEIGHT", config->namcoES1.ffbWeight);
    config->namcoES1.ffbSpringGain =
        getInt(ini, "NamcoES1", "FFB_SPRING", config->namcoES1.ffbSpringGain);
    config->namcoES1.ffbDamperGain =
        getInt(ini, "NamcoES1", "FFB_DAMPER", config->namcoES1.ffbDamperGain);
    config->namcoES1.ffbVibrationGain =
        getInt(ini, "NamcoES1", "FFB_VIBRATION", config->namcoES1.ffbVibrationGain);
    config->namcoES1.ffbBaseDamper =
        getInt(ini, "NamcoES1", "FFB_BASE_DAMPER", config->namcoES1.ffbBaseDamper);
    config->namcoES1.ffbDeadband =
        getInt(ini, "NamcoES1", "FFB_DEADBAND", config->namcoES1.ffbDeadband);
    config->namcoES1.ffbRumbleDuration = getInt(
        ini, "NamcoES1", "FFB_RUMBLE_DURATION", config->namcoES1.ffbRumbleDuration);
    config->namcoES1.ffbInvert =
        getInt(ini, "NamcoES1", "FFB_INVERT", config->namcoES1.ffbInvert);
    config->namcoES1.card.enabled =
        getInt(ini, "NamcoES1", "YACARDEMU_ENABLED", config->namcoES1.card.enabled);
    config->namcoES1.card.autoStart =
        getInt(ini, "NamcoES1", "YACARDEMU_AUTOSTART", config->namcoES1.card.autoStart);
    getString(ini, "NamcoES1", "YACARDEMU_PATH", config->namcoES1.card.executablePath,
              sizeof(config->namcoES1.card.executablePath));
    getString(ini, "NamcoES1", "YACARDEMU_PIPE", config->namcoES1.card.pipeName,
              sizeof(config->namcoES1.card.pipeName));
    getString(ini, "NamcoES1", "YACARDEMU_API_HOST", config->namcoES1.card.apiHost,
              sizeof(config->namcoES1.card.apiHost));
    config->namcoES1.card.apiPort =
        getInt(ini, "NamcoES1", "YACARDEMU_API_PORT", config->namcoES1.card.apiPort);
    getString(ini, "NamcoES1", "YACARDEMU_CARD_NAME", config->namcoES1.card.cardName,
              sizeof(config->namcoES1.card.cardName));
    config->namcoES1.card.diagnostics =
        getInt(ini, "NamcoES1", "YACARDEMU_DIAGNOSTICS", config->namcoES1.card.diagnostics);
    config->namcoES1.icCard.enabled =
        getInt(ini, "NamcoES1", "IC_CARD_ENABLED", config->namcoES1.icCard.enabled);
    config->namcoES1.icCard.autoInsert =
        getInt(ini, "NamcoES1", "IC_CARD_AUTO_INSERT", config->namcoES1.icCard.autoInsert);
    config->namcoES1.icCard.diagnostics =
        getInt(ini, "NamcoES1", "IC_CARD_DIAGNOSTICS", config->namcoES1.icCard.diagnostics);
    getString(ini, "NamcoES1", "IC_CARD_FILE", config->namcoES1.icCard.cardFile,
              sizeof(config->namcoES1.icCard.cardFile));
    config->namcoES1.legacyCard.enabled = getInt(
        ini, "NamcoES1", "LEGACY_CARD_ENABLED", config->namcoES1.legacyCard.enabled);
    config->namcoES1.legacyCard.autoInsert = getInt(
        ini, "NamcoES1", "LEGACY_CARD_AUTO_INSERT", config->namcoES1.legacyCard.autoInsert);
    config->namcoES1.legacyCard.diagnostics = getInt(
        ini, "NamcoES1", "LEGACY_CARD_DIAGNOSTICS", config->namcoES1.legacyCard.diagnostics);
    getString(ini, "NamcoES1", "LEGACY_CARD_FILE", config->namcoES1.legacyCard.cardFile,
              sizeof(config->namcoES1.legacyCard.cardFile));

    // [NamcoN2]
    getString(ini, "NamcoN2", "DONGLE_ID", config->namcoN2.dongleId,
              sizeof(config->namcoN2.dongleId));
    getString(ini, "NamcoN2", "DONGLE_ID_2", config->namcoN2.dongleId2,
              sizeof(config->namcoN2.dongleId2));
    config->namcoN2.debugMode =
        getInt(ini, "NamcoN2", "DEBUG_MODE", config->namcoN2.debugMode);

    // Advanced keys remain readable for existing installations, but are not
    // emitted into a new linuxloader.ini because the defaults match N2 hardware.
    config->namcoN2.forceFeedbackEnabled =
        getInt(ini, "NamcoN2", "FFB_ENABLED", config->namcoN2.forceFeedbackEnabled);
    config->namcoN2.forceFeedbackDiagnostics = getInt(
        ini, "NamcoN2", "FFB_DIAGNOSTICS", config->namcoN2.forceFeedbackDiagnostics);
    config->namcoN2.ffbGain =
        getInt(ini, "NamcoN2", "FFB_GAIN", config->namcoN2.ffbGain);
    config->namcoN2.ffbSpringGain =
        getInt(ini, "NamcoN2", "FFB_SPRING_GAIN", config->namcoN2.ffbSpringGain);
    config->namcoN2.ffbDamperGain =
        getInt(ini, "NamcoN2", "FFB_DAMPER_GAIN", config->namcoN2.ffbDamperGain);
    config->namcoN2.ffbVibrationGain =
        getInt(ini, "NamcoN2", "FFB_VIBRATION_GAIN", config->namcoN2.ffbVibrationGain);
    config->namcoN2.ffbReflectRange =
        getInt(ini, "NamcoN2", "FFB_REFLECT_RANGE", config->namcoN2.ffbReflectRange);
    config->namcoN2.ffbInvert =
        getInt(ini, "NamcoN2", "FFB_INVERT", config->namcoN2.ffbInvert);
    config->namcoN2.ffbDamperFloor =
        getInt(ini, "NamcoN2", "FFB_DAMPER_FLOOR", config->namcoN2.ffbDamperFloor);
    config->namcoN2.ffbSpringDeadband = getInt(
        ini, "NamcoN2", "FFB_SPRING_DEADBAND", config->namcoN2.ffbSpringDeadband);
    config->namcoN2.steering.minimum =
        getInt(ini, "NamcoN2", "STEERING_RAW_MIN", config->namcoN2.steering.minimum);
    config->namcoN2.steering.maximum =
        getInt(ini, "NamcoN2", "STEERING_RAW_MAX", config->namcoN2.steering.maximum);
    config->namcoN2.accelerator.minimum = getInt(
        ini, "NamcoN2", "ACCELERATOR_RAW_MIN", config->namcoN2.accelerator.minimum);
    config->namcoN2.accelerator.maximum = getInt(
        ini, "NamcoN2", "ACCELERATOR_RAW_MAX", config->namcoN2.accelerator.maximum);
    config->namcoN2.brake.minimum =
        getInt(ini, "NamcoN2", "BRAKE_RAW_MIN", config->namcoN2.brake.minimum);
    config->namcoN2.brake.maximum =
        getInt(ini, "NamcoN2", "BRAKE_RAW_MAX", config->namcoN2.brake.maximum);

    getString(ini, "NamcoN2", "JVS_IO_NAME", config->namcoN2.jvs.name,
              sizeof(config->namcoN2.jvs.name));
    config->namcoN2.jvs.players =
        getInt(ini, "NamcoN2", "JVS_PLAYERS", config->namcoN2.jvs.players);
    config->namcoN2.jvs.switches =
        getInt(ini, "NamcoN2", "JVS_SWITCHES", config->namcoN2.jvs.switches);
    config->namcoN2.jvs.coins =
        getInt(ini, "NamcoN2", "JVS_COINS", config->namcoN2.jvs.coins);
    config->namcoN2.jvs.analogueInputs = getInt(
        ini, "NamcoN2", "JVS_ANALOGUE_IN", config->namcoN2.jvs.analogueInputs);
    config->namcoN2.jvs.generalPurposeOutputs = getInt(
        ini, "NamcoN2", "JVS_GPO", config->namcoN2.jvs.generalPurposeOutputs);
    config->namcoN2.jvs.analogueOutputs = getInt(
        ini, "NamcoN2", "JVS_ANALOGUE_OUT", config->namcoN2.jvs.analogueOutputs);
    config->namcoN2.jvs.generalPurposeInputs = getInt(
        ini, "NamcoN2", "JVS_GPI", config->namcoN2.jvs.generalPurposeInputs);
    config->namcoN2.card.enabled =
        getInt(ini, "NamcoN2", "YACARDEMU_ENABLED", config->namcoN2.card.enabled);
    config->namcoN2.card.autoStart =
        getInt(ini, "NamcoN2", "YACARDEMU_AUTOSTART", config->namcoN2.card.autoStart);
    getString(ini, "NamcoN2", "YACARDEMU_PATH",
              config->namcoN2.card.executablePath, sizeof(config->namcoN2.card.executablePath));
    getString(ini, "NamcoN2", "YACARDEMU_PIPE",
              config->namcoN2.card.pipeName, sizeof(config->namcoN2.card.pipeName));
    getString(ini, "NamcoN2", "YACARDEMU_API_HOST",
              config->namcoN2.card.apiHost, sizeof(config->namcoN2.card.apiHost));
    config->namcoN2.card.apiPort =
        getInt(ini, "NamcoN2", "YACARDEMU_API_PORT", config->namcoN2.card.apiPort);
    getString(ini, "NamcoN2", "YACARDEMU_CARD_NAME",
              config->namcoN2.card.cardName, sizeof(config->namcoN2.card.cardName));
    config->namcoN2.card.diagnostics =
        getInt(ini, "NamcoN2", "YACARDEMU_DIAGNOSTICS", config->namcoN2.card.diagnostics);

    config->namcoN2.network.enabled =
        getInt(ini, "NamcoN2", "NETWORK_ENABLED", config->namcoN2.network.enabled);
    getString(ini, "NamcoN2", "NETWORK_INTERFACE", config->namcoN2.network.interfaceName,
              sizeof(config->namcoN2.network.interfaceName));
    getString(ini, "NamcoN2", "NETWORK_BIND_ADDRESS", config->namcoN2.network.bindAddress,
              sizeof(config->namcoN2.network.bindAddress));
    getString(ini, "NamcoN2", "NETWORK_BROADCAST_ADDRESS",
              config->namcoN2.network.broadcastAddress,
              sizeof(config->namcoN2.network.broadcastAddress));
    config->namcoN2.network.allowBroadcast = getInt(
        ini, "NamcoN2", "NETWORK_ALLOW_BROADCAST", config->namcoN2.network.allowBroadcast);
    config->namcoN2.network.rewriteBroadcast = getInt(
        ini, "NamcoN2", "NETWORK_REWRITE_BROADCAST", config->namcoN2.network.rewriteBroadcast);

    // [Display]
    config->width = getInt(ini, "Display", "WIDTH", config->width);
    config->height = getInt(ini, "Display", "HEIGHT", config->height);
    config->boostRenderRes = getInt(ini, "Display", "BOOST_RENDER_RES", config->boostRenderRes);
    config->fullscreen = getInt(ini, "Display", "FULLSCREEN", config->fullscreen);
    config->borderEnabled = getInt(ini, "Display", "BORDER_ENABLED", config->borderEnabled);
	config->whiteBorderPercentage = getFloat(ini, "Display", "WHITE_BORDER_PERCENTAGE", config->whiteBorderPercentage * 100.0f) / 100.0f;
	config->blackBorderPercentage = getFloat(ini, "Display", "BLACK_BORDER_PERCENTAGE", config->blackBorderPercentage * 100.0f) / 100.0f;
    config->keepAspectRatio = getInt(ini, "Display", "KEEP_ASPECT_RATIO", config->keepAspectRatio);
    config->hideCursor = getInt(ini, "Display", "HIDE_CURSOR", config->hideCursor);

    config->inputMode = getInt(ini, "Input", "INPUT_MODE", config->inputMode);

    // [Emulation]
    const char *regionRaw = getValue(ini, "Emulation", "REGION");
    if (regionRaw)
    {
        char cleanBuffer[16];
        char *regionStr = cleanValue(regionRaw, cleanBuffer, sizeof(cleanBuffer));
        if (strcmp(regionStr, "JP") == 0)
            config->region = JP;
        else if (strcmp(regionStr, "US") == 0)
            config->region = US;
        else if (strcmp(regionStr, "EX") == 0)
            config->region = EX;
    }
    config->freeplay = getInt(ini, "Emulation", "FREEPLAY", config->freeplay);
    config->emulateJVS = getInt(ini, "Emulation", "EMULATE_JVS", config->emulateJVS);
    config->emulateRideboard = getInt(ini, "Emulation", "EMULATE_RIDEBOARD", config->emulateRideboard);
    config->emulateDriveboard = getInt(ini, "Emulation", "EMULATE_DRIVEBOARD", config->emulateDriveboard);
    config->emulateMotionboard = getInt(ini, "Emulation", "EMULATE_MOTIONBOARD", config->emulateMotionboard);
    config->emulateHW210CardReader = getInt(ini, "Emulation", "EMULATE_HW210_CARDREADER", config->emulateHW210CardReader);
    config->emulateIDCardReader = getInt(ini, "Emulation", "EMULATE_ID_CARDREADER", config->emulateIDCardReader);
    config->emulateTouchscreen = getInt(ini, "Emulation", "EMULATE_TOUCHSCREEN", config->emulateTouchscreen);



    // [Graphics]
    config->hummerFlickerFix = getInt(ini, "Graphics", "HUMMER_FLICKER_FIX", config->hummerFlickerFix);
    config->fpsLimiter = getInt(ini, "Graphics", "FPS_LIMITER_ENABLED", config->fpsLimiter);
    config->fpsTarget = getFloat(ini, "Graphics", "FPS_TARGET", config->fpsTarget);
    config->vsync = getInt(ini, "Graphics", "VSYNC", config->vsync);
    config->lgjRenderWithMesa = getInt(ini, "Graphics", "LGJ_RENDER_WITH_MESA", config->lgjRenderWithMesa);
    config->disableBuiltinFont = getInt(ini, "Graphics", "DISABLE_BUILTIN_FONT", config->disableBuiltinFont);
    config->disableBuiltinLogos = getInt(ini, "Graphics", "DISABLE_BUILTIN_LOGOS", config->disableBuiltinLogos);

    config->mj4EnabledAtT = getInt(ini, "GameSpecific", "MJ4_ENABLED_ALL_THE_TIME", config->mj4EnabledAtT);
    config->idSteeringPercentageReduction =
        getFloat(ini, "GameSpecific", "ID_STEERING_REDUCTION_PERCENTAGE", config->idSteeringPercentageReduction);

    config->showDebugMessages = getInt(ini, "System", "DEBUG_MSGS", config->showDebugMessages);
    config->useAltJvsPassthrough = getInt(ini, "System", "USE_ALT_JVS_PASSTHROUGH", config->useAltJvsPassthrough);



}

int initConfig(const char *configFilePath)
{
    setDefaultValues(&config);

    config.crc32 = partialElfCrc;
    if (detectGame(config.crc32) != 0)
    {
        log_error("Unsupported N2/ES1 game CRC: 0x%X", config.crc32);
    }

    config.inputMode = 0;

    char filePath[PATH_MAX] = "";
    if (configFilePath != NULL && configFilePath[0] != '\0')
    {
        strncpy(filePath, configFilePath, PATH_MAX - 1);
    }
    else
    {
        if (fileExists(CONFIG_PATH))
        {
            strncpy(filePath, CONFIG_PATH, PATH_MAX - 1);
        }
    }
    filePath[PATH_MAX - 1] = '\0';

    /* No explicit file and no pacloader.ini in the working directory means
     * "use defaults", not an attempt to open an empty filename. */
    if (filePath[0] == '\0')
        return 0;

    IniConfig *ini = iniLoad(filePath);

    if (ini == NULL)
    {
        log_warn("Cannot open or parse %s, using default values.", filePath);
        return 1;
    }

    applyIniConfig(&config, ini);

    iniFree(ini);

    return 0;
}

EmulatorConfig *getConfig()
{
    return &config;
}
