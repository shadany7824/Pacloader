#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <libgen.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../graphics/blitStretching.h"
#include "../config/config.h"
#include "../hardware/ffb/sdlFfbBackend.h"
#include "../hardware/common/cardControl.h"
#include "../config/iniParser.h"
#include "sdlInput.h"
#include "../hardware/common/jvs.h"
#include "../log/log.h"
#include "../graphics/sdlCalls.h"
#include "../mainShared.h"

// --- GLOBAL STATE AND MAPPINGS ---
ActionState gActionStates[MAX_ENTITIES][NUM_LOGICAL_ACTIONS];
JVSActionMapping gJvsMap[MAX_ENTITIES][NUM_LOGICAL_ACTIONS];
ChangedAction gChangedActions[NUM_LOGICAL_ACTIONS * MAX_ENTITIES];
static bool gChangedActionFlags[MAX_ENTITIES][NUM_LOGICAL_ACTIONS];
static ChangedAction gCombinedAxes[MAX_PLAYERS * NUM_LOGICAL_ACTIONS];
static int gNumCombinedAxes = 0;
LogicalActionProperties gActionProperties[MAX_ENTITIES][NUM_LOGICAL_ACTIONS];
int gNumChangedActions = 0;
SDLControllers sdlJoysticks;
float gLastGunNormX[MAX_ENTITIES];
float gLastGunNormY[MAX_ENTITIES];
int gLastGunXDir[MAX_ENTITIES];
int gLastGunYDir[MAX_ENTITIES];
float gShakeValue[MAX_ENTITIES];
/* Gears the sequential GearUp/GearDown bindings walk through.  Only six shifter
 * positions have switch patterns, so this is capped where it is used. */
int gShifterGears = 6;
float gShakeIncreaseRate = 10.0f;
float gShakeDecayRate = 0.95f;
float gShakeMinScreenFraction = 0.15f;

ComboGroup gComboGroups[MAX_COMBINATION_GROUPS] = {0};
int gNumComboGroups = 0;

// --- NEW GLOBALS FOR GUID MAPPING ---
char gPlayerGUIDs[MAX_PLAYERS + 1][33]; // +1 for SYSTEM, 33 for 32 chars + null terminator
bool gPlayerGUIDsDirty = false;         // Flag to check if we need to save the INI

// Direct lookup tables for high-performance event handling
BindingPair gKeyBindings[SDL_SCANCODE_COUNT];
BindingPair gMouseButtonBindings[MAX_MOUSE_BUTTONS];
ControlBinding gMouseAxisBindings[2]; // 0=X, 1=Y
BindingPair gJoyAxisBindings[MAX_JOYSTICKS][MAX_JOY_AXES];
BindingPair gJoyButtonBindings[MAX_JOYSTICKS][MAX_JOY_BUTTONS];
BindingPair gControllerAxisBindings[MAX_JOYSTICKS][SDL_GAMEPAD_AXIS_COUNT];
BindingPair gControllerButtonBindings[MAX_JOYSTICKS][SDL_GAMEPAD_BUTTON_COUNT];
HatBinding gJoyHatBindings[MAX_JOYSTICKS][MAX_JOY_HATS];

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;
int widthOri;
int heightOri;
extern SDL_Window *g_SdlWindow;
extern bool sdlInputInitialized;

extern Dest dest;

extern int phX, phY, phW, phH;
int phIsDragging = 0;

extern bool mj4MousePressed;
int mj4MouseX = 0;
int mj4MouseY = 0;
extern int drawableW;
extern int drawableH;
extern bool mj4TouchedInsideScreen;


int jvsAnalogueMaxValue;
int jvsAnalogueCenterValue;

GameType gameType;

// Map between string names and LogicalAction enums for INI parsing.
const struct
{
    const char *name;
    LogicalAction action;
} gActionNameMap[] = {{"Test", LA_Test},
                      {"Enter", LA_Enter},
                      {"TestUp", LA_TestUp},
                      {"TestDown", LA_TestDown},
                      {"Nitro", LA_Nitro},
                      {"3DChange", LA_3DChange},
                      {"Keypad1", LA_Keypad1},
                      {"Keypad2", LA_Keypad2},
                      {"Keypad3", LA_Keypad3},
                      {"Keypad4", LA_Keypad4},
                      {"Keypad5", LA_Keypad5},
                      {"Keypad6", LA_Keypad6},
                      {"Keypad7", LA_Keypad7},
                      {"Keypad8", LA_Keypad8},
                      {"Keypad9", LA_Keypad9},
                      {"KeypadStar", LA_KeypadStar},
                      {"Keypad0", LA_Keypad0},
                      {"KeypadHash", LA_KeypadHash},
                      {"Coin", LA_Coin},
                      {"GearUp", LA_GearUp},
                      {"GearDown", LA_GearDown},
                      {"Gear1", LA_Gear1},
                      {"Gear2", LA_Gear2},
                      {"Gear3", LA_Gear3},
                      {"Gear4", LA_Gear4},
                      {"Gear5", LA_Gear5},
                      {"Gear6", LA_Gear6},
                      {"ViewChange", LA_ViewChange},
                      {"Intrude", LA_Intrude},
                      {"MusicChange", LA_MusicChange},
                      {"Boost", LA_Boost},
                      {"BoostRight", LA_BoostRight},
                      {"Start", LA_Start},
                      {"Service", LA_Service},
                      {"Up", LA_Up},
                      {"Down", LA_Down},
                      {"Left", LA_Left},
                      {"Right", LA_Right},
                      {"Button1", LA_Button1},
                      {"Button2", LA_Button2},
                      {"Button3", LA_Button3},
                      {"Button4", LA_Button4},
                      {"Button5", LA_Button5},
                      {"Button6", LA_Button6},
                      {"Button7", LA_Button7},
                      {"Button8", LA_Button8},
                      {"Button9", LA_Button9},
                      {"Button10", LA_Button10},
                      {"Trigger", LA_Trigger},
                      {"OutOfScreen", LA_OutOfScreen},
                      {"Reload", LA_Reload},
                      {"GunButton", LA_GunButton},
                      {"ActionButton", LA_ActionButton},
                      {"PedalLeft", LA_PedalLeft},
                      {"PedalRight", LA_PedalRight},
                      {"Steer", LA_Steer},
                      {"Gas", LA_Gas},
                      {"Brake", LA_Brake},
                      {"GunX", LA_GunX},
                      {"GunY", LA_GunY},
                      {"Steer_Left", LA_Steer_Left},
                      {"Steer_Right", LA_Steer_Right},
                      {"Gas_Digital", LA_Gas_Digital},
                      {"Brake_Digital", LA_Brake_Digital},
                      {"Flying_X", LA_Flying_X},
                      {"Flying_Left", LA_Flying_Left},
                      {"Flying_Right", LA_Flying_Right},
                      {"Flying_Y", LA_Flying_Y},
                      {"Flying_Up", LA_Flying_Up},
                      {"Flying_Down", LA_Flying_Down},
                      {"GunTrigger", LA_GunTrigger},
                      {"MissileTrigger", LA_MissileTrigger},
                      {"ClimaxSwitch", LA_ClimaxSwitch},
                      {"Throttle", LA_Throttle},
                      {"Throttle_Accelerate", LA_Throttle_Accelerate},
                      {"Throttle_Slowdown", LA_Throttle_Slowdown},
                      {"CardInsert", LA_CardInsert},
                      {"Card1Insert", LA_Card1Insert},
                      {"Card2Insert", LA_Card2Insert},
                      {"CardEject", LA_CardEject},
                      {"ButtonA", LA_A},
                      {"ButtonB", LA_B},
                      {"ButtonC", LA_C},
                      {"ButtonD", LA_D},
                      {"ButtonE", LA_E},
                      {"ButtonF", LA_F},
                      {"ButtonG", LA_G},
                      {"ButtonH", LA_H},
                      {"ButtonI", LA_I},
                      {"ButtonJ", LA_J},
                      {"ButtonK", LA_K},
                      {"ButtonL", LA_L},
                      {"ButtonM", LA_M},
                      {"ButtonN", LA_N},
                      {"ButtonReach", LA_Reach},
                      {"ButtonChi", LA_Chi},
                      {"ButtonPon", LA_Pon},
                      {"ButtonKan", LA_Kan},
                      {"ButtonAgari", LA_Agari},
                      {"ButtonCancel", LA_Cancel},
                      {"ExitGame", LA_ExitGame}};

const int NUM_ACTION_NAMES = sizeof(gActionNameMap) / sizeof(gActionNameMap[0]);

// --- Default Bindings ---
// These arrays define the built-in control schemes that are used when controls.ini is not found.
// They are also used by createDefaultControlsIni() to generate a fresh INI file.
extern const ControlBinding gDefaultCommonBindings[];
extern const ControlBinding gDefaultDigitalBindings[];
extern const ControlBinding gDefaultDrivingBindings[];
extern const ControlBinding gDefaultWmmtBindings[];
extern const ControlBinding gDefaultWmmt4Bindings[];
extern const ControlBinding gDefaultMaximumHeat3dBindings[];
extern const ControlBinding gDefaultFlyingBindings[];
extern const ControlBinding gDefaultShootingBindings[];
extern const ControlBinding gDefaultMahjongBindings[];

extern const size_t gDefaultCommonBindingsSize;
extern const size_t gDefaultDigitalBindingsSize;
extern const size_t gDefaultDrivingBindingsSize;
extern const size_t gDefaultWmmtBindingsSize;
extern const size_t gDefaultWmmt4BindingsSize;
extern const size_t gDefaultMaximumHeat3dBindingsSize;
extern const size_t gDefaultFlyingBindingsSize;
extern const size_t gDefaultShootingBindingsSize;
extern const size_t gDefaultMahjongBindingsSize;

#ifndef COMPILING_LINUXLOADER_ELF

// Forward declaration
void saveGuidsToIni();

/* Brings up SDL input: subsystems, controllers, and the bindings from
 * controls.ini or the built-in defaults.  Returns 0 on success. */
int initSdlInput(const char *controlsPath)
{
    if (getConfig()->inputMode == 2)
        return 0;

    // Initialize GameController subsystem
    if (!SDL_Init(SDL_INIT_GAMEPAD))
        log_warn("Could not initialize SDL_GameController: %s\n", SDL_GetError());

    // Load official and community-sourced controller mappings from a database file.
    char *envDbPath = getenv("LINUX_LOADER_CONTROLS_DB_PATH");
    if (envDbPath)
    {
        if (SDL_AddGamepadMappingsFromFile(envDbPath) > 0)
            printf("%s loaded as a controller database.\n", myBasename(envDbPath));
        else
            printf("Failed to load %s as a controller database.\n", myBasename(envDbPath));
    }

    gameType = getConfig()->gameType;
    jvsAnalogueMaxValue = (1 << getJVSIO()->capabilities.analogueInBits) - 1;
    jvsAnalogueCenterValue = jvsAnalogueMaxValue / 2;
    memset(gPlayerGUIDs, 0, sizeof(gPlayerGUIDs));

    // Set initial analog values for specific games that require it.
    if (gGrp == GROUP_HOD4 || gGrp == GROUP_HOD4_TEST)
    {
        setAnalogue(ANALOGUE_5, jvsAnalogueCenterValue);
        setAnalogue(ANALOGUE_6, jvsAnalogueCenterValue);
        setAnalogue(ANALOGUE_7, jvsAnalogueCenterValue);
        setAnalogue(ANALOGUE_8, jvsAnalogueCenterValue);
    }
    else if (gId == HARLEY_DAVIDSON_SBRG)
    {
        setAnalogue(ANALOGUE_2, jvsAnalogueCenterValue);
    }
    else if (gameType == DRIVING)
    {
        setAnalogue(ANALOGUE_1, jvsAnalogueCenterValue);
        setAnalogue(ANALOGUE_5, jvsAnalogueCenterValue);
    }

    // Initialize all mappings and properties.
    initJvsMappings();
    initActionProperties();

    // Load control bindings from specified file on tries to load it from the current folder, otherwise use defaults.
    struct stat buffer;
    IniConfig *ini;
    int isProfileLoaded = 0;
    if (stat(controlsPath, &buffer) == 0)
    {
        log_debug("controls config file loaded from %s\n", controlsPath);
        ini = iniLoad(controlsPath);
    }
    else
    {
        log_debug("controls config file \"controls.ini\" loaded from current folder\n");
        ini = iniLoad("controls.ini");
    }

    if (ini)
    {
        printf("Found controls.ini, loading custom configuration...\n");
        loadGlobalConfig(ini);
        loadProfileFromIni(iniGetSection(ini, "Common"));
        // Namco N2 reports itself as a driving game but has its own panel.
        if (gGrp == GROUP_WMMT3)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "WMMT"));
        else if (gGrp == GROUP_WMMT4_ES1)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "WMMT4"));
        else if (gGrp == GROUP_MAXIMUM_HEAT_3D)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "MaximumHeat3D"));
        else if (gameType == DRIVING)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "Driving"));
        else if (gameType == DIGITAL)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "Digital"));
        else if (gameType == SHOOTING)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "Shooting"));
        else if (gameType == FLYING)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "Flying"));
        else if (gameType == MAHJONG)
            isProfileLoaded = loadProfileFromIni(iniGetSection(ini, "Mahjong"));

        iniFree(ini);
    }

    if (!isProfileLoaded)
    {
        setDefaultMappings();
    }

    // --- NEW GUID-BASED CONTROLLER MAPPING ---
    int numJoysticks;
    SDL_JoystickID *joystics = SDL_GetJoysticks(&numJoysticks);
    int playerToDeviceIndex[MAX_PLAYERS + 1];
    for (int i = 0; i <= MAX_PLAYERS; i++)
        playerToDeviceIndex[i] = -1; // -1 means unassigned

    if (joystics)
    {
        bool deviceIsClaimed[MAX_JOYSTICKS] = {false};
        SDL_JoystickID instanceId;
        // First Pass: Match saved GUIDs to available hardware
        printf("Attempting to map controllers by saved GUIDs...\n");
        for (int player = 1; player <= MAX_PLAYERS; player++)
        {
            if (strlen(gPlayerGUIDs[player]) > 0 && isProfileLoaded)
            {
                for (int i = 0; i < numJoysticks; i++)
                {
                    if (deviceIsClaimed[i])
                        continue;

                    instanceId = joystics[i];
                    SDL_GUID guid = SDL_GetJoystickGUIDForID(instanceId);
                    char device_guid_str[33];
                    SDL_GUIDToString(guid, device_guid_str, sizeof(device_guid_str));
                    device_guid_str[4] = '0';
                    device_guid_str[5] = '0';
                    device_guid_str[6] = '0';
                    device_guid_str[7] = '0';
                    if (strcmp(gPlayerGUIDs[player], device_guid_str) == 0)
                    {
                        printf("  - Matched P%d to device %d ('%s') via GUID.\n", player, i, SDL_GetJoystickNameForID(instanceId));
                        playerToDeviceIndex[player] = instanceId;
                        deviceIsClaimed[i] = true;
                        break;
                    }
                }
            }
        }

        // Second Pass: Assign remaining players to unclaimed controllers
        printf("Assigning remaining players to available controllers...\n");
        for (int player = 1; player <= MAX_PLAYERS; player++)
        {
            if (playerToDeviceIndex[player] == -1) // If this player is still unassigned
            {
                for (int i = 0; i < numJoysticks; i++)
                {
                    if (!deviceIsClaimed[i]) // Find the first unclaimed device
                    {
                        instanceId = joystics[i];
                        printf("  - Assigned P%d to first available device %d ('%s').\n", player, i, SDL_GetJoystickNameForID(instanceId));
                        playerToDeviceIndex[player] = instanceId;
                        deviceIsClaimed[i] = true;

                        // Get its GUID and mark for saving.
                        SDL_GUID guid = SDL_GetJoystickGUIDForID(instanceId);
                        SDL_GUIDToString(guid, gPlayerGUIDs[player], sizeof(gPlayerGUIDs[player]));
                        gPlayerGUIDs[player][4] = '0';
                        gPlayerGUIDs[player][5] = '0';
                        gPlayerGUIDs[player][6] = '0';
                        gPlayerGUIDs[player][7] = '0';
                        gPlayerGUIDsDirty = true;
                        break;
                    }
                }
            }
        }

        // Now, open devices in player-centric order.
        // The index `i` in sdlJoysticks (0..MAX_JOYSTICKS-1) now corresponds to Player (i+1).
        // This makes JOY0_, GC0_ always refer to P1, etc.
        sdlJoysticks.joysticksCount = 0;
        for (int player = 1; player <= MAX_PLAYERS; player++)
        {
            int deviceIndex = playerToDeviceIndex[player];
            int internal_index = player - 1;

            if (deviceIndex != -1)
            {
                if (SDL_IsGamepad(deviceIndex))
                {
                    sdlJoysticks.controllers[internal_index] = SDL_OpenGamepad(deviceIndex);
                }
                else
                {
                    sdlJoysticks.joysticks[internal_index] = SDL_OpenJoystick(deviceIndex);
                }

                if (sdlJoysticks.controllers[internal_index] || sdlJoysticks.joysticks[internal_index])
                {
                    sdlJoysticks.joysticksCount++;
                }
            }
        }
        SDL_free(joystics);
    }

    if (sdlJoysticks.joysticksCount > 0)
    {
        sdlFfbInit();
    }

#ifdef __linux__
    findAndOpenWiiMotes(&sdlJoysticks);
    startWiimoteThreads();
#endif

    // Save any new GUID assignments back to the INI file.
    if (isProfileLoaded)
        saveGuidsToIni();

    // After loading all bindings, scan them to identify combined axes.
    detectCombinedAxes();

    // Apply any final game-specific mapping overrides.
    remapPerGame();

    sdlInputInitialized = true;
    return 0;
}

/* Splits an INI key such as "P1_Start" into its player and action. */
bool parseActionKey(const char *key, JVSPlayer *out_player, LogicalAction *out_action)
{
    char genericKey[64];
    int p_num;
    if (sscanf(key, "P%d_%s", &p_num, genericKey) == 2)
    {
        if (p_num >= 1 && p_num <= MAX_PLAYERS)
            *out_player = (JVSPlayer)p_num;
        else
            return false;
    }
    else
    {
        *out_player = PLAYER_1;
        strcpy(genericKey, key);
        if (strcmp(genericKey, "Test") == 0||strcmp(genericKey, "ExitGame") == 0)
            *out_player = SYSTEM;
    }
    for (int i = 0; i < NUM_ACTION_NAMES; i++)
    {
        if (strcmp(gActionNameMap[i].name, genericKey) == 0)
        {
            *out_action = gActionNameMap[i].action;
            return true;
        }
    }
    return false;
}

/* Default LogicalAction to JVS mapping: which actions are switches, which are
 * analogue channels, and which are coin drops. */
void initJvsMappings()
{
    for (int p = 0; p < MAX_ENTITIES; p++)
    {
        for (int i = 0; i < NUM_LOGICAL_ACTIONS; i++)
            gJvsMap[p][i] = (JVSActionMapping){JVS_CALL_NONE, NONE};
    }

    gJvsMap[SYSTEM][LA_Test] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_TEST};

    for (int p = 1; p <= MAX_PLAYERS; p++)
    {
        gJvsMap[p][LA_Start] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_START};
        gJvsMap[p][LA_Service] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_SERVICE};
        gJvsMap[p][LA_Coin] = (JVSActionMapping){JVS_CALL_COIN, COIN};

        int shootingPlayerIdx = (p - 1) * 2;

        if (gameType == DIGITAL)
        {
            gJvsMap[p][LA_Up] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
            gJvsMap[p][LA_Down] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[p][LA_Left] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_LEFT};
            gJvsMap[p][LA_Right] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};
            gJvsMap[p][LA_Button1] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[p][LA_Button2] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
            gJvsMap[p][LA_Button3] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        }
        else if (gameType == SHOOTING)
        {
            gJvsMap[p][LA_GunX] = (JVSActionMapping){JVS_CALL_ANALOGUE, (JVSInput)(ANALOGUE_1 + shootingPlayerIdx)};
            gJvsMap[p][LA_GunY] = (JVSActionMapping){JVS_CALL_ANALOGUE, (JVSInput)(ANALOGUE_2 + shootingPlayerIdx)};
            gJvsMap[p][LA_Trigger] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[p][LA_Reload] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
            gJvsMap[p][LA_GunButton] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
            gJvsMap[p][LA_ActionButton] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
            gJvsMap[p][LA_ChangeButton] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_5};
            gJvsMap[p][LA_PedalLeft] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_LEFT};
            gJvsMap[p][LA_PedalRight] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};
        }
    }

    switch ((int)gameType)
    {
        case DRIVING:
        {
            gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[PLAYER_2][LA_GearUp] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
            gJvsMap[PLAYER_2][LA_GearDown] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[PLAYER_2][LA_Boost] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[PLAYER_1][LA_CardInsert] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};

            gJvsMap[PLAYER_1][LA_Steer] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Gas] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
            gJvsMap[PLAYER_1][LA_Brake] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_3};
            gJvsMap[PLAYER_1][LA_Gas_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
            gJvsMap[PLAYER_1][LA_Brake_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_3};
            gJvsMap[PLAYER_1][LA_Steer_Left] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Steer_Right] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Up] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
            gJvsMap[PLAYER_1][LA_Down] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[PLAYER_1][LA_Left] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_LEFT};
            gJvsMap[PLAYER_1][LA_Right] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};

            gJvsMap[PLAYER_2][LA_Steer] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_5};
            gJvsMap[PLAYER_2][LA_Gas] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_6};
            gJvsMap[PLAYER_2][LA_Brake] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_7};
            gJvsMap[PLAYER_2][LA_Gas_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_6};
            gJvsMap[PLAYER_2][LA_Brake_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_7};
            gJvsMap[PLAYER_2][LA_Steer_Left] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_5};
            gJvsMap[PLAYER_2][LA_Steer_Right] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_5};
        }
        break;
        case FLYING:
        {
            gJvsMap[PLAYER_1][LA_Flying_X] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Flying_Left] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Flying_Right] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
            gJvsMap[PLAYER_1][LA_Flying_Y] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
            gJvsMap[PLAYER_1][LA_Flying_Up] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
            gJvsMap[PLAYER_1][LA_Flying_Down] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
            gJvsMap[PLAYER_1][LA_Throttle] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_3};
            gJvsMap[PLAYER_1][LA_Throttle_Accelerate] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_3};
            gJvsMap[PLAYER_1][LA_Throttle_Slowdown] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_3};
            gJvsMap[PLAYER_1][LA_GunTrigger] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[PLAYER_1][LA_MissileTrigger] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
            gJvsMap[PLAYER_1][LA_ClimaxSwitch] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        }
        break;
        case MAHJONG:
        {
            gJvsMap[PLAYER_1][LA_A] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};
            gJvsMap[PLAYER_1][LA_B] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_LEFT};
            gJvsMap[PLAYER_1][LA_C] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
            gJvsMap[PLAYER_1][LA_D] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[PLAYER_1][LA_E] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[PLAYER_1][LA_F] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
            gJvsMap[PLAYER_1][LA_G] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
            gJvsMap[PLAYER_1][LA_H] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
            gJvsMap[PLAYER_1][LA_I] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_5};
            gJvsMap[PLAYER_1][LA_J] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_6};
            gJvsMap[PLAYER_1][LA_K] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_7};
            gJvsMap[PLAYER_2][LA_L] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};
            gJvsMap[PLAYER_2][LA_M] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_LEFT};
            gJvsMap[PLAYER_2][LA_N] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
            gJvsMap[PLAYER_2][LA_Chi] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
            gJvsMap[PLAYER_2][LA_Pon] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
            gJvsMap[PLAYER_2][LA_Kan] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
            gJvsMap[PLAYER_2][LA_Reach] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
            gJvsMap[PLAYER_2][LA_Agari] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
            gJvsMap[PLAYER_2][LA_Cancel] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_5};
            gJvsMap[PLAYER_2][LA_CardInsert] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_7};
        }
        break;
    }
    gJvsMap[PLAYER_1][LA_Card1Insert] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
    gJvsMap[PLAYER_2][LA_Card2Insert] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
}

/* Patches the default mapping for titles whose panel differs from it. */
void remapPerGame()
{
    if (gGrp == GROUP_WMMT3)
    {
        /* The positions the cabinet's JVIO board reports, so the JVS state is
         * wire-correct.  GearUp/GearDown stay on PLAYER_2 for compatibility with
         * existing controls.ini files and drive the sequential shifter. */
        gJvsMap[PLAYER_1][LA_Up] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        gJvsMap[PLAYER_1][LA_Down] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
        gJvsMap[PLAYER_1][LA_Left] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_5};
        gJvsMap[PLAYER_1][LA_Right] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_6};
        gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_9};
        gJvsMap[PLAYER_1][LA_Intrude] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_10};

        // The cabinet has no separate start switch; the panel button the game
        // reads as start is wired to the service line.
        gJvsMap[PLAYER_1][LA_Start] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_SERVICE};

        /* The DRIVING default puts CardInsert on the switch N2 uses for the
         * shifter, and WMMT3 reads its card through /dev/ttyM2 anyway. */
        gJvsMap[PLAYER_1][LA_CardInsert] = (JVSActionMapping){JVS_CALL_NONE, NONE};
    }
    else if (gGrp == GROUP_WMMT4_ES1)
    {
        /* The switch bits the cabinet's JVS board reports.  The shifter is an H
         * pattern whose gears each close two of them, so it is applied by
         * updateWmmtEs1Shifter() rather than mapped action by action. */
        gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_9};
        gJvsMap[PLAYER_1][LA_Intrude] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_10};
        gJvsMap[PLAYER_1][LA_Enter] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
        gJvsMap[PLAYER_1][LA_TestUp] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
        gJvsMap[PLAYER_1][LA_TestDown] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
        gJvsMap[PLAYER_1][LA_Service] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_SERVICE};
        gJvsMap[PLAYER_2][LA_GearUp] = (JVSActionMapping){JVS_CALL_NONE, NONE};
        gJvsMap[PLAYER_2][LA_GearDown] = (JVSActionMapping){JVS_CALL_NONE, NONE};

        /* The card goes through the reader on ttyS1, not a panel switch. */
        gJvsMap[PLAYER_1][LA_CardInsert] = (JVSActionMapping){JVS_CALL_NONE, NONE};
    }
    else if (gGrp == GROUP_MAXIMUM_HEAT_3D)
    {
        /* Maximum Heat 3D's ES1 Jamma panel: clInputDeviceJamma::update()
         * translates only these switch bits.  VIEW CHANGE doubles as "decide"
         * and steps the calibration that writes testmode_if_initialize.bin,
         * without which the wheel boots pinned at full lock. */
        gJvsMap[PLAYER_1][LA_Service] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_SERVICE};
        gJvsMap[PLAYER_1][LA_Enter] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
        gJvsMap[PLAYER_1][LA_TestUp] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
        gJvsMap[PLAYER_1][LA_TestDown] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
        gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_5};
        gJvsMap[PLAYER_1][LA_Nitro] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};

        /* With STR_BRAKE_DIGITAL set in data/csv/config.csv the cabinet takes the
         * brake from switch bit 0x0080 and ignores analogue channel 3, so a pedal
         * has to close that switch through the axis' digital mode. */
        gJvsMap[PLAYER_1][LA_Brake] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        gJvsMap[PLAYER_1][LA_Brake_Digital] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};

        /* 2D/3D CHANGE has no switch yet: update() does not translate BUTTON_2,
         * and the remaining candidates need the test mode SWITCH TEST screen to
         * settle which one the panel uses. */
        gJvsMap[PLAYER_1][LA_3DChange] = (JVSActionMapping){JVS_CALL_NONE, NONE};
        gJvsMap[PLAYER_1][LA_Keypad1] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_1};
        gJvsMap[PLAYER_1][LA_Keypad2] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_2};
        gJvsMap[PLAYER_1][LA_Keypad3] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_3};
        gJvsMap[PLAYER_1][LA_Keypad4] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_4};
        gJvsMap[PLAYER_1][LA_Keypad5] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_5};
        gJvsMap[PLAYER_1][LA_Keypad6] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_6};
        gJvsMap[PLAYER_1][LA_Keypad7] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_7};
        gJvsMap[PLAYER_1][LA_Keypad8] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_8};
        gJvsMap[PLAYER_1][LA_Keypad9] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_9};
        gJvsMap[PLAYER_1][LA_KeypadStar] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_STAR};
        gJvsMap[PLAYER_1][LA_Keypad0] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_0};
        gJvsMap[PLAYER_1][LA_KeypadHash] = (JVSActionMapping){JVS_CALL_KEYPAD, KEYPAD_HASH};
    }
    else if (gId == R_TUNED_SBQW)
    {
        gJvsMap[PLAYER_1][LA_BoostRight] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_RIGHT};
        gJvsMap[PLAYER_1][LA_CardInsert] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_UP};
    }
    else if (gGrp == GROUP_HUMMER)
        gJvsMap[PLAYER_2][LA_Boost] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_DOWN};
    else if (gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID4_JAP || gGrp == GROUP_ID5)
        gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
    else if (gId == LETS_GO_JUNGLE_SBLU || gId == LETS_GO_JUNGLE_SBLU_REVA)
    {
        gJvsMap[PLAYER_1][LA_GunX] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
        gJvsMap[PLAYER_1][LA_GunY] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
        gJvsMap[PLAYER_2][LA_GunX] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
        gJvsMap[PLAYER_2][LA_GunY] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
    }
    else if (gId == HARLEY_DAVIDSON_SBRG)
    {
        gJvsMap[PLAYER_1][LA_Steer] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
        gJvsMap[PLAYER_1][LA_Steer_Left] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
        gJvsMap[PLAYER_1][LA_Steer_Right] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_2};
        gJvsMap[PLAYER_1][LA_Gas] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
        gJvsMap[PLAYER_1][LA_Gas_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_1};
        gJvsMap[PLAYER_1][LA_Brake] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_4};
        gJvsMap[PLAYER_1][LA_Brake_Digital] = (JVSActionMapping){JVS_CALL_ANALOGUE, ANALOGUE_4};
        gJvsMap[PLAYER_1][LA_MusicChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_1};
        gJvsMap[PLAYER_1][LA_ViewChange] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
        gJvsMap[PLAYER_1][LA_GearUp] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        gJvsMap[PLAYER_1][LA_GearDown] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_4};
    }
    else if (gId == PRIMEVAL_HUNT_SBPP)
    {
        gJvsMap[PLAYER_1][LA_Reload] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
        gJvsMap[PLAYER_1][LA_GunButton] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
        gJvsMap[PLAYER_2][LA_Reload] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_2};
        gJvsMap[PLAYER_2][LA_GunButton] = (JVSActionMapping){JVS_CALL_SWITCH, BUTTON_3};
    }
}

/* Per-action properties, such as which analogue actions auto-centre like a stick
 * and which rest at zero like a pedal. */
void initActionProperties()
{
    for (int p = 0; p < MAX_ENTITIES; p++)
    {
        for (int i = 0; i < NUM_LOGICAL_ACTIONS; i++)
        {
            gActionProperties[p][i].isCentering = false;
            gActionProperties[p][i].isCombinedAxis = false;
            // Default deadzone for most actions is a sensible 8000
            gActionProperties[p][i].deadzone = 8000;
        }
        // Explicitly define which actions are centering (like sticks)
        gActionProperties[p][LA_Steer].isCentering = true;

        // Set smaller default deadzones for non-centering actions
        gActionProperties[p][LA_Gas].deadzone = 500;
        gActionProperties[p][LA_Brake].deadzone = 500;
        
        // Default card insert actions to toggle behavior
        gActionProperties[p][LA_CardInsert].isToggle = true;
        gActionProperties[p][LA_Card1Insert].isToggle = true;
        gActionProperties[p][LA_Card2Insert].isToggle = true;
        if (gGrp == GROUP_WMMT3 || gGrp == GROUP_WMMT4_ES1)
        {
            // YaCardEmu controls are commands, not persistent JVS switches.
            gActionProperties[p][LA_CardInsert].isToggle = false;
        }
    }
    /* The cabinet's TEST switch is a latching one, so holding a key down is not
     * how it is operated; controls.ini can still override this with TestToggle. */
    if (gGrp == GROUP_WMMT4_ES1)
        gActionProperties[SYSTEM][LA_Test].isToggle = true;

    gActionProperties[PLAYER_1][LA_Flying_X].isCentering = true;
    gActionProperties[PLAYER_1][LA_Flying_Y].isCentering = true;
    gActionProperties[PLAYER_1][LA_Throttle].isCentering = true;
}

/* Adds a binding and reports conflicts.  Axes accept two bindings so a
 * split-axis configuration can use both halves. */
void addBinding(ControlBinding binding)
{
    char name1[128], name2[128];
    char inputName[64] = "Unknown";

    // Handle non-axis types: buttons, keys, mouse buttons.
    // These now use BindingPair to allow a combo and a standalone binding on the same physical input.
    if (binding.type != INPUT_TYPE_JOY_AXIS && binding.type != INPUT_TYPE_GAMEPAD_AXIS)
    {
        BindingPair *pair = NULL;
        switch (binding.type)
        {
            case INPUT_TYPE_KEY:
                if (binding.sdlId < SDL_SCANCODE_COUNT)
                {
                    pair = &gKeyBindings[binding.sdlId];
                    snprintf(inputName, sizeof(inputName), "KEY_%s", SDL_GetScancodeName((SDL_Scancode)binding.sdlId));
                }
                break;
            case INPUT_TYPE_MOUSE_BUTTON:
                if (binding.sdlId < MAX_MOUSE_BUTTONS)
                {
                    pair = &gMouseButtonBindings[binding.sdlId];
                    snprintf(inputName, sizeof(inputName), "MOUSE_BUTTON_%d", binding.sdlId);
                }
                break;
            case INPUT_TYPE_MOUSE_AXIS:
                if (binding.sdlId < 2)
                {
                    // Mouse axes are special — keep single binding behavior
                    ControlBinding *existing = &gMouseAxisBindings[binding.sdlId];
                    snprintf(inputName, sizeof(inputName), "MOUSE_AXIS_%d", binding.sdlId);
                    if (existing->type != INPUT_TYPE_NONE && existing->action != binding.action)
                    {
                        getLogicalActionString(existing, name1, sizeof(name1), "");
                        getLogicalActionString(&binding, name2, sizeof(name2), "");
                        fprintf(stderr, "ERROR: Input assignment conflict for '%s'.\n", inputName);
                        fprintf(stderr, "       It is mapped to both '%s' and '%s'. Please fix controls.ini.\n", name1, name2);
                    }
                    else
                    {
                        *existing = binding;
                    }
                    return;
                }
                break;
            case INPUT_TYPE_JOY_BUTTON:
                if (binding.deviceIndex < MAX_JOYSTICKS && binding.sdlId < MAX_JOY_BUTTONS)
                {
                    pair = &gJoyButtonBindings[binding.deviceIndex][binding.sdlId];
                    snprintf(inputName, sizeof(inputName), "JOY%d_BUTTON_%d", binding.deviceIndex, binding.sdlId);
                }
                break;
            case INPUT_TYPE_GAMEPAD_BUTTON:
                if (binding.deviceIndex < MAX_JOYSTICKS && binding.sdlId < SDL_GAMEPAD_BUTTON_COUNT)
                {
                    pair = &gControllerButtonBindings[binding.deviceIndex][binding.sdlId];
                    snprintf(inputName, sizeof(inputName), "GC%d_BUTTON_%s", binding.deviceIndex,
                             SDL_GetGamepadStringForButton((SDL_GamepadButton)binding.sdlId));
                }
                break;
            default:
                break;
        }

        if (pair)
        {
            if (pair->bindings[0].type == INPUT_TYPE_NONE)
            {
                pair->bindings[0] = binding;
            }
            else if (pair->bindings[1].type == INPUT_TYPE_NONE)
            {
                pair->bindings[1] = binding;
            }
            else if (pair->bindings[0].action == binding.action && pair->bindings[0].player == binding.player)
            {
                pair->bindings[0] = binding;
            }
            else if (pair->bindings[1].action == binding.action && pair->bindings[1].player == binding.player)
            {
                pair->bindings[1] = binding;
            }
            else
            {
                getLogicalActionString(&pair->bindings[0], name1, sizeof(name1), "");
                getLogicalActionString(&binding, name2, sizeof(name2), "");
                fprintf(stderr, "ERROR: Cannot bind more than two actions to '%s'. Please fix controls.ini.\n", inputName);
            }
        }
        return;
    }

    if (binding.type == INPUT_TYPE_JOY_HAT)
    {
        if (binding.deviceIndex < MAX_JOYSTICKS && binding.sdlId < MAX_JOY_HATS)
        {
            HatBinding *hat = &gJoyHatBindings[binding.deviceIndex][binding.sdlId];
            ControlBinding *targetSlot = NULL;

            // Determine which directional slot to use
            if (binding.axisThreshold == SDL_HAT_UP)
                targetSlot = &hat->up;
            else if (binding.axisThreshold == SDL_HAT_DOWN)
                targetSlot = &hat->down;
            else if (binding.axisThreshold == SDL_HAT_LEFT)
                targetSlot = &hat->left;
            else if (binding.axisThreshold == SDL_HAT_RIGHT)
                targetSlot = &hat->right;

            if (targetSlot)
            {
                if (targetSlot->type != INPUT_TYPE_NONE && targetSlot->action != binding.action)
                {
                    if (targetSlot->comboGroupId != -1 || binding.comboGroupId != -1)
                    {
                        // Suppress conflict error if it involves a combination
                    }
                    else
                    {
                        printf("Conflict detection\n");
                    }
                }
                else
                    *targetSlot = binding;
            }
        }
        return;
    }

    // Special handling for axis types to allow two bindings per physical axis.
    BindingPair *pair = NULL;
    int devIdx = binding.deviceIndex;
    int axisId = binding.sdlId;

    if (binding.type == INPUT_TYPE_JOY_AXIS)
    {
        if (devIdx < MAX_JOYSTICKS && axisId < MAX_JOY_AXES)
        {
            pair = &gJoyAxisBindings[devIdx][axisId];
            snprintf(inputName, sizeof(inputName), "JOY%d_AXIS_%d", devIdx, axisId);
        }
    }
    else if (binding.type == INPUT_TYPE_GAMEPAD_AXIS)
    {
        if (devIdx < MAX_JOYSTICKS && axisId < SDL_GAMEPAD_AXIS_COUNT)
        {
            pair = &gControllerAxisBindings[devIdx][axisId];
            snprintf(inputName, sizeof(inputName), "GC%d_AXIS_%s", devIdx, SDL_GetGamepadStringForAxis((SDL_GamepadAxis)axisId));
        }
    }

    if (pair)
    {
        // Find an empty slot in the pair to place the binding.
        if (pair->bindings[0].type == INPUT_TYPE_NONE)
        {
            pair->bindings[0] = binding;
        }
        else if (pair->bindings[1].type == INPUT_TYPE_NONE)
        {
            // Check for redundant bindings before adding.
            if (pair->bindings[0].action == binding.action)
            {
                getLogicalActionString(&binding, name1, sizeof(name1), "");
                fprintf(stderr, "WARNING: Redundant binding for '%s' on action '%s'. Check controls.ini.\n", inputName, name1);
            }
            pair->bindings[1] = binding;
        }
        else
        {
            fprintf(stderr, "ERROR: Cannot bind more than two actions to axis '%s'. Please fix controls.ini.\n", inputName);
        }
    }
}

/* Applies the built-in bindings when controls.ini is absent. */
void setDefaultMappings()
{
    log_warn("Applying default mappings...\n");

    // Add common bindings
    for (size_t i = 0; i < gDefaultCommonBindingsSize; i++)
    {
        addBinding(gDefaultCommonBindings[i]);
    }

    // Add game-specific bindings
    const ControlBinding *game_bindings = NULL;
    size_t bindingsCount = 0;

    if (gGrp == GROUP_WMMT3)
    {
        game_bindings = gDefaultWmmtBindings;
        bindingsCount = gDefaultWmmtBindingsSize;
    }
    else if (gGrp == GROUP_WMMT4_ES1)
    {
        game_bindings = gDefaultWmmt4Bindings;
        bindingsCount = gDefaultWmmt4BindingsSize;
    }
    else if (gGrp == GROUP_MAXIMUM_HEAT_3D)
    {
        game_bindings = gDefaultMaximumHeat3dBindings;
        bindingsCount = gDefaultMaximumHeat3dBindingsSize;
    }
    else if (gameType == DIGITAL)
    {
        game_bindings = gDefaultDigitalBindings;
        bindingsCount = gDefaultDigitalBindingsSize;
    }
    else if (gameType == DRIVING)
    {
        game_bindings = gDefaultDrivingBindings;
        bindingsCount = gDefaultDrivingBindingsSize;
    }
    else if (gameType == FLYING)
    {
        game_bindings = gDefaultFlyingBindings;
        bindingsCount = gDefaultFlyingBindingsSize;
    }
    else if (gameType == SHOOTING)
    {
        game_bindings = gDefaultShootingBindings;
        bindingsCount = gDefaultShootingBindingsSize;
    }
    else if (gameType == MAHJONG)
    {
        game_bindings = gDefaultMahjongBindings;
        bindingsCount = gDefaultMahjongBindingsSize;
    }

    if (game_bindings)
    {
        for (size_t i = 0; i < bindingsCount; i++)
        {
            addBinding(game_bindings[i]);
        }
    }
}

/* Turns one input token - "KEY_A", "JOY0_AXIS_0_NEGATIVE" and so on - into a
 * ControlBinding for this player and action. */
void parseSdlSource(const char *token, JVSPlayer player, LogicalAction action, int comboGroupId, int comboInputIndex)
{
    ControlBinding map;
    memset(&map, 0, sizeof(map));
    map.action = action;
    map.player = player;
    map.type = INPUT_TYPE_NONE;
    map.comboGroupId = comboGroupId;
    map.comboInputIndex = comboInputIndex;
    char mutableToken[128];
    strncpy(mutableToken, token, 127);
    mutableToken[127] = '\0';
    char *invertedStr = strstr(mutableToken, "_INVERTED");
    if (invertedStr)
    {
        map.isInverted = true;
        *invertedStr = '\0';
    }
    else
    {
        map.isInverted = false;
    }

    if (strncmp(mutableToken, "KEY_", 4) == 0)
    {
        map.type = INPUT_TYPE_KEY;
        if (strcmp(mutableToken + 4, "Comma") == 0 || strcmp(mutableToken + 4, "COMMA") == 0)
            map.sdlId = SDL_SCANCODE_COMMA;
        else
            map.sdlId = SDL_GetScancodeFromName(mutableToken + 4);
    }
    else if (strncmp(mutableToken, "GC", 2) == 0)
    {
        int devId;
        char typeStr[32], nameStr[64];
        if (sscanf(mutableToken, "GC%d_%[^_]_%[^_]", &devId, typeStr, nameStr) == 3)
        {
            map.deviceIndex = devId;
            if (strcmp(typeStr, "BUTTON") == 0)
            {
                map.type = INPUT_TYPE_GAMEPAD_BUTTON;
                map.sdlId = SDL_GetGamepadButtonFromString(nameStr);
            }
            else if (strcmp(typeStr, "AXIS") == 0)
            {
                map.type = INPUT_TYPE_GAMEPAD_AXIS;
                map.sdlId = SDL_GetGamepadAxisFromString(nameStr);
                // Check for axis mode suffixes
                if (strstr(mutableToken, "_POSITIVE_HALF"))
                {
                    map.axisMode = AXIS_MODE_POSITIVE_HALF;
                }
                else if (strstr(mutableToken, "_NEGATIVE_HALF"))
                {
                    map.axisMode = AXIS_MODE_NEGATIVE_HALF;
                }
                else if (strstr(mutableToken, "_POSITIVE"))
                {
                    map.axisMode = AXIS_MODE_DIGITAL;
                    map.axisThreshold = 1;
                }
                else if (strstr(mutableToken, "_NEGATIVE"))
                {
                    map.axisMode = AXIS_MODE_DIGITAL;
                    map.axisThreshold = -1;
                }
                else
                {
                    map.axisMode = AXIS_MODE_FULL;
                }
            }
        }
    }
    else if (strncmp(mutableToken, "JOY", 3) == 0)
    {
        int devId, compId;
        char dirStr[32];
        if (sscanf(mutableToken, "JOY%d_BUTTON_%d", &devId, &compId) == 2)
        {
            map.type = INPUT_TYPE_JOY_BUTTON;
            map.deviceIndex = devId;
            map.sdlId = compId;
        }
        else if (sscanf(mutableToken, "JOY%d_AXIS_%d", &devId, &compId) == 2)
        {
            map.type = INPUT_TYPE_JOY_AXIS;
            map.deviceIndex = devId;
            map.sdlId = compId;
            if (strstr(mutableToken, "_POSITIVE_HALF"))
                map.axisMode = AXIS_MODE_POSITIVE_HALF;
            else if (strstr(mutableToken, "_NEGATIVE_HALF"))
                map.axisMode = AXIS_MODE_NEGATIVE_HALF;
            else if (strstr(mutableToken, "_POSITIVE"))
            {
                map.axisMode = AXIS_MODE_DIGITAL;
                map.axisThreshold = 1;
            }
            else if (strstr(mutableToken, "_NEGATIVE"))
            {
                map.axisMode = AXIS_MODE_DIGITAL;
                map.axisThreshold = -1;
            }
            else
                map.axisMode = AXIS_MODE_FULL;
        }
        else if (sscanf(mutableToken, "JOY%d_HAT%d_%s", &devId, &compId, dirStr) == 3)
        {
            map.type = INPUT_TYPE_JOY_HAT;
            map.deviceIndex = devId;
            map.sdlId = compId;
            if (strcmp(dirStr, "UP") == 0)
                map.axisThreshold = SDL_HAT_UP;
            else if (strcmp(dirStr, "DOWN") == 0)
                map.axisThreshold = SDL_HAT_DOWN;
            else if (strcmp(dirStr, "LEFT") == 0)
                map.axisThreshold = SDL_HAT_LEFT;
            else if (strcmp(dirStr, "RIGHT") == 0)
                map.axisThreshold = SDL_HAT_RIGHT;
        }
    }
    else if (strncmp(mutableToken, "MOUSE_", 6) == 0)
    {
        const char *mouseToken = mutableToken + 6;
        if (strcmp(mouseToken, "AXIS_X") == 0)
        {
            map.type = INPUT_TYPE_MOUSE_AXIS;
            map.sdlId = 0;
        }
        else if (strcmp(mouseToken, "AXIS_Y") == 0)
        {
            map.type = INPUT_TYPE_MOUSE_AXIS;
            map.sdlId = 1;
        }
        else if (strcmp(mouseToken, "LEFT_BUTTON") == 0)
        {
            map.type = INPUT_TYPE_MOUSE_BUTTON;
            map.sdlId = SDL_BUTTON_LEFT;
        }
        else if (strcmp(mouseToken, "RIGHT_BUTTON") == 0)
        {
            map.type = INPUT_TYPE_MOUSE_BUTTON;
            map.sdlId = SDL_BUTTON_RIGHT;
        }
        else if (strcmp(mouseToken, "MIDDLE_BUTTON") == 0)
        {
            map.type = INPUT_TYPE_MOUSE_BUTTON;
            map.sdlId = SDL_BUTTON_MIDDLE;
        }
    }
    if (map.type != INPUT_TYPE_NONE)
        addBinding(map);
}

/* Player index for a GameController, or -1 when the id is not one of ours. */
int getControllerID(SDL_JoystickID instance_id)
{
    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        if (sdlJoysticks.controllers[i])
        {
            SDL_Joystick *joy = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
            if (joy && SDL_GetJoystickID(joy) == instance_id)
                return i;
        }
    }
    return -1;
}

/* Player index for a raw joystick, or -1 when the id is not one of ours. */
int getJoystickID(SDL_JoystickID instance_id)
{
    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        if (sdlJoysticks.joysticks[i])
        {
            if (SDL_GetJoystickID(sdlJoysticks.joysticks[i]) == instance_id)
                return i;
        }
    }
    return -1;
}

/* Corrects the player for actions the cabinet wires to a fixed one, such as
 * the shifter paddles. */
JVSPlayer fixPlayerForAction(LogicalAction action, int player)
{
    if (action == LA_Boost)
        return PLAYER_2;

    if ((action == LA_GearUp || action == LA_GearDown) && gId != HARLEY_DAVIDSON_SBRG)
        return PLAYER_2;

    return (JVSPlayer)player;
}

/* True for the mahjong actions that belong to player 2. */
bool isMahjongP2(LogicalAction action)
{
    switch (action)
    {
        case LA_L:
        case LA_M:
        case LA_N:
        case LA_Chi:
        case LA_Pon:
        case LA_Kan:
        case LA_Reach:
        case LA_Agari:
        case LA_Cancel:
        case LA_CardInsert:
            return true;
        default:
            return false;
    }
}

/* Loads every binding in one INI section. */
static void parseAndApplyBindings(const char *value, JVSPlayer player, LogicalAction action)
{
    char *value_copy = strdup(value);
    char *orSavePtr;
    char *orToken = strtok_r(value_copy, ",", &orSavePtr);
    while (orToken != NULL)
    {
        if (strchr(orToken, '+') != NULL)
        {
            int comboId = gNumComboGroups++;
            if (comboId >= MAX_COMBINATION_GROUPS)
                break;

            gComboGroups[comboId].action = action;
            gComboGroups[comboId].player = player;
            gComboGroups[comboId].numInputs = 0;
            gComboGroups[comboId].activeCount = 0;

            char *andSavePtr;
            char *andToken = strtok_r(orToken, "+", &andSavePtr);
            while (andToken != NULL && gComboGroups[comboId].numInputs < MAX_INPUTS_PER_COMBO)
            {
                while (*andToken == ' ')
                    andToken++;
                char *end = andToken + strlen(andToken) - 1;
                while (end > andToken && *end == ' ')
                {
                    *end = '\0';
                    end--;
                }

                if (strlen(andToken) > 0)
                {
                    parseSdlSource(andToken, player, action, comboId, gComboGroups[comboId].numInputs++);
                }
                andToken = strtok_r(NULL, "+", &andSavePtr);
            }
        }
        else
        {
            while (*orToken == ' ')
                orToken++;
            char *end = orToken + strlen(orToken) - 1;
            while (end > orToken && *end == ' ')
            {
                *end = '\0';
                end--;
            }

            if (strlen(orToken) > 0)
            {
                parseSdlSource(orToken, player, action, -1, 0);
            }
        }

        orToken = strtok_r(NULL, ",", &orSavePtr);
    }
    free(value_copy);
}

int loadProfileFromIni(const IniSection *section)
{
    if (!section)
        return 0;

    bool exitGameFound = false;

    for (int i = 0; i < section->numPairs; i++)
    {
        /* Maximum Heat 3D's TEST switch is a mode selector: it stays asserted
         * until the next press.  Kept profile-local so no other title sees it. */
        if (strcmp(section->name, "MaximumHeat3D") == 0 &&
            strcmp(section->pairs[i].key, "TestToggle") == 0)
        {
            gActionProperties[SYSTEM][LA_Test].isToggle = atoi(section->pairs[i].value) != 0;
            continue;
        }

        JVSPlayer player;
        LogicalAction action;
        if (!parseActionKey(section->pairs[i].key, &player, &action))
        {
            printf("Warning: Unknown action '%s'\n", section->pairs[i].key);
            continue;
        }

        if (action == LA_ExitGame)
            exitGameFound = true;

        if (player != SYSTEM && (action != LA_Card1Insert || action != LA_Card2Insert))
            player = fixPlayerForAction(action, player);

        if (action == LA_Card2Insert && gameType == DIGITAL)
            player = PLAYER_2;

        if (strcmp(section->name, "Mahjong") == 0 && isMahjongP2(action))
            player = PLAYER_2;

        parseAndApplyBindings(section->pairs[i].value, player, action);
    }

    if (strcmp(section->name, "Common") == 0 && !exitGameFound)
    {
        parseAndApplyBindings("KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK, JOY0_BUTTON_11 + JOY0_BUTTON_10", SYSTEM, LA_ExitGame);
    }

    return 1;
}

/* Loads the [Config] section, including the saved controller GUIDs. */
int getShifterGears(void)
{
    return gShifterGears;
}

int getWmmtDirectGear(void)
{
    static const LogicalAction gears[] = {
        LA_Gear1, LA_Gear2, LA_Gear3, LA_Gear4, LA_Gear5, LA_Gear6,
    };
    for (int gear = 0; gear < 6; gear++)
    {
        if (gActionStates[PLAYER_1][gears[gear]].isActive)
            return gear + 1;
    }
    return 0;
}

void loadGlobalConfig(const IniConfig *ini)
{
    const IniSection *configSection = iniGetSection(ini, "Config");
    if (configSection)
    {
        printf("Loading global settings from [Config] section...\n");
        for (int i = 0; i < configSection->numPairs; i++)
        {
            const char *key = configSection->pairs[i].key;
            const char *value = configSection->pairs[i].value;
            int p_num = -1;
            char actionKeyBuffer[64];

            // Check for player-specific prefix like "P1_"
            if (sscanf(key, "P%d_%s", &p_num, actionKeyBuffer) == 2)
            {
                // Player-specific key found (e.g., "P1_Steer_DeadZone")
                char *deadzoneSuffix = strstr(actionKeyBuffer, "_DeadZone");
                if (deadzoneSuffix)
                {
                    *deadzoneSuffix = '\0'; // Trim to just the action name
                    for (int j = 0; j < NUM_ACTION_NAMES; j++)
                    {
                        if (strcmp(gActionNameMap[j].name, actionKeyBuffer) == 0)
                        {
                            LogicalAction action = gActionNameMap[j].action;
                            if (p_num >= 1 && p_num <= MAX_PLAYERS)
                            {
                                gActionProperties[p_num][action].deadzone = atoi(value);
                                printf("  Set P%d_%s DeadZone to %d\n", p_num, actionKeyBuffer, gActionProperties[p_num][action].deadzone);
                            }
                            break;
                        }
                    }
                }
                
                char *toggleSuffix = strstr(actionKeyBuffer, "_Toggle");
                if (toggleSuffix)
                {
                    *toggleSuffix = '\0'; // Trim to just the action name
                    for (int j = 0; j < NUM_ACTION_NAMES; j++)
                    {
                        if (strcmp(gActionNameMap[j].name, actionKeyBuffer) == 0)
                        {
                            LogicalAction action = gActionNameMap[j].action;
                            if (p_num >= 1 && p_num <= MAX_PLAYERS)
                            {
                                gActionProperties[p_num][action].isToggle = (atoi(value) != 0);
                                printf("  Set P%d_%s Toggle to %d\n", p_num, actionKeyBuffer, gActionProperties[p_num][action].isToggle);
                            }
                            break;
                        }
                    }
                }
            }
            else
            {
                // Handle generic keys and other settings
                char *deadzonePos = strstr(key, "_DeadZone");
                if (deadzonePos != NULL)
                {
                    // Generic key found (e.g., "Steer_DeadZone")
                    // Apply this setting to ALL players as a default
                    size_t actionNameLen = deadzonePos - key;
                    strncpy(actionKeyBuffer, key, actionNameLen);
                    actionKeyBuffer[actionNameLen] = '\0';

                    for (int j = 0; j < NUM_ACTION_NAMES; j++)
                    {
                        if (strcmp(gActionNameMap[j].name, actionKeyBuffer) == 0)
                        {
                            LogicalAction action = gActionNameMap[j].action;
                            int deadzoneVal = atoi(value);
                            for (int p = 1; p <= MAX_PLAYERS; p++)
                            {
                                // Set this only if a player-specific one wasn't already set
                                // (This requires ordering in the INI, or a second pass - for simplicity, we just set it)
                                gActionProperties[p][action].deadzone = deadzoneVal;
                            }
                            printf("  Set %s DeadZone to %d for all players (default)\n", actionKeyBuffer, deadzoneVal);
                            break;
                        }
                    }
                }
                
                char *togglePos = (char *)strstr(key, "_Toggle");
                if (togglePos != NULL)
                {
                    size_t actionNameLen = togglePos - key;
                    strncpy(actionKeyBuffer, key, actionNameLen);
                    actionKeyBuffer[actionNameLen] = '\0';

                    for (int j = 0; j < NUM_ACTION_NAMES; j++)
                    {
                        if (strcmp(gActionNameMap[j].name, actionKeyBuffer) == 0)
                        {
                            LogicalAction action = gActionNameMap[j].action;
                            bool toggleVal = (atoi(value) != 0);
                            for (int p = 0; p < MAX_ENTITIES; p++)
                            {
                                gActionProperties[p][action].isToggle = toggleVal;
                            }
                            printf("  Set %s Toggle to %d for all players (default)\n", actionKeyBuffer, toggleVal);
                            break;
                        }
                    }
                }
                else if (strcmp(key, "ShifterGears") == 0)
                {
                    gShifterGears = atoi(value);
                    printf("  Set ShifterGears to %d\n", gShifterGears);
                }
                else if (strcmp(key, "ShakeIncreaseRate") == 0)
                {
                    gShakeIncreaseRate = atof(value);
                    printf("  Set ShakeIncreaseRate to %f\n", gShakeIncreaseRate);
                }
                else if (strcmp(key, "ShakeDecayRate") == 0)
                {
                    gShakeDecayRate = atof(value);
                    printf("  Set ShakeDecayRate to %f\n", gShakeDecayRate);
                }
                else if (strcmp(key, "ShakeMinScreenFraction") == 0)
                {
                    gShakeMinScreenFraction = atof(value);
                    printf("  Set ShakeMinScreenFraction to %f\n", gShakeMinScreenFraction);
                }
            }
        }
    }

    // Load saved controller GUIDs
    const IniSection *guidSection = iniGetSection(ini, "ControllerGUIDs");
    if (guidSection)
    {
        printf("Loading saved controller GUIDs...\n");
        for (int i = 0; i < guidSection->numPairs; i++)
        {
            int p_num;
            if (sscanf(guidSection->pairs[i].key, "P%d_GUID", &p_num) == 1)
            {
                if (p_num >= 1 && p_num <= MAX_PLAYERS)
                {
                    strncpy(gPlayerGUIDs[p_num], guidSection->pairs[i].value, 32);
                    gPlayerGUIDs[p_num][32] = '\0';
                    printf("  - Loaded P%d GUID: %s\n", p_num, gPlayerGUIDs[p_num]);
                }
            }
        }
    }
}

/* Queues an action for the frame-end pass, so several changes between frames
 * cost one update rather than several. */
void addActionToDirtyList(JVSPlayer player, LogicalAction action)
{
    if (gChangedActionFlags[player][action])
        return; // Already dirty

    gChangedActionFlags[player][action] = true;
    gChangedActions[gNumChangedActions++] = (ChangedAction){player, action};
}

void updateBindingState(ControlBinding *binding, bool isActive, float analogValue)
{
    if (!binding || binding->type == INPUT_TYPE_NONE)
        return;

    if (binding->comboGroupId == -1)
    {
        bool isToggle = gActionProperties[binding->player][binding->action].isToggle;
        ActionState *state = &gActionStates[binding->player][binding->action];

        if (isToggle)
        {
            if (isActive && !state->isPhysicalActive)
            {
                state->isActive = !state->isActive;
                state->analogValue = state->isActive ? 1.0f : 0.0f;
                if (state->isActive)
                    state->lastActivatedAt = SDL_GetTicks();
                addActionToDirtyList(binding->player, binding->action);
            }
            state->isPhysicalActive = isActive;
        }
        else
        {
            if (state->isActive != isActive)
            {
                state->isActive = isActive;
                state->analogValue = analogValue;
                if (isActive)
                    state->lastActivatedAt = SDL_GetTicks();
                addActionToDirtyList(binding->player, binding->action);
            }
        }
    }
    else
    {
        ComboGroup *combo = &gComboGroups[binding->comboGroupId];
        if (combo->inputStates[binding->comboInputIndex] != isActive)
        {
            combo->inputStates[binding->comboInputIndex] = isActive;
            combo->activeCount += isActive ? 1 : -1;

            bool actionActive = (combo->activeCount == combo->numInputs);
            bool isToggle = gActionProperties[combo->player][combo->action].isToggle;
            ActionState *state = &gActionStates[combo->player][combo->action];

            if (isToggle)
            {
                if (actionActive && !state->isPhysicalActive)
                {
                    state->isActive = !state->isActive;
                    state->analogValue = state->isActive ? 1.0f : 0.0f;
                    if (state->isActive)
                        state->lastActivatedAt = SDL_GetTicks();
                    addActionToDirtyList(combo->player, combo->action);
                }
                state->isPhysicalActive = actionActive;
            }
            else
            {
                if (state->isActive != actionActive)
                {
                    state->isActive = actionActive;
                    state->analogValue = actionActive ? 1.0f : 0.0f;
                    if (actionActive)
                        state->lastActivatedAt = SDL_GetTicks();
                    addActionToDirtyList(combo->player, combo->action);
                }
            }
        }
    }
}

bool isActionActiveOrRecentlyPressed(JVSPlayer player, LogicalAction action,
                                     Uint64 minimumPulseMs)
{
    if (player < SYSTEM || player >= MAX_ENTITIES ||
        action < 0 || action >= NUM_LOGICAL_ACTIONS)
        return false;

    const ActionState *state = &gActionStates[player][action];
    if (state->isActive)
        return true;
    if (state->lastActivatedAt == 0 || minimumPulseMs == 0)
        return false;

    return SDL_GetTicks() - state->lastActivatedAt < minimumPulseMs;
}

/*
 * Continuous analog sources can produce many events between two presents.
 * Their state is already retained in gActionStates, so flushing JVS for every
 * one only repeats work that the frame-end pass will do again.  Keep the
 * immediate path for edge-triggered inputs such as buttons and TEST.
 */
static bool eventIsContinuousAnalog(const SDL_Event *e)
{
    if (!e)
        return false;

    switch (e->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            return true;
#ifdef __linux__
        case SDL_WIIMOTION_EVENT:
            return true;
#endif
        default:
            return false;
    }
}

/* Main event handler: maps one SDL event onto the logical action it drives. */
void processSdlEvent(const SDL_Event *e)
{
#ifdef __linux__
    // Make sure to use the variable, not the macro name
    if (e->type == SDL_WIIMOTION_EVENT)
    {
        int controllerIndex = e->user.code;
        int irX = (intptr_t)e->user.data1;
        int irY = (intptr_t)e->user.data2;

        if (irX == 0 || irX == 1023 || irY == 0 || irY == 767)
        {
            gActionStates[controllerIndex + 1][LA_Reload].isActive = true;
            addActionToDirtyList(controllerIndex + 1, LA_Reload);
        }
        else
        {
            gActionStates[controllerIndex + 1][LA_Reload].isActive = false;
            addActionToDirtyList(controllerIndex + 1, LA_Reload);
        }

        // The Wii Remote IR gives 0-1023 for X and 0-767 for Y.
        // We can normalize this to 0.0 - 1.0 to behave like our mouse input.
        float posX = 1.0f - (irX / 1023.0f);
        float posY = irY / 767.0f;

        // printf("idx: %d, x: %f, y: %f\n", controller_index, posX, posY);

        ActionState *stateX = &gActionStates[controllerIndex + 1][LA_GunX];
        if (fabs(stateX->analogValue - posX) > 0.001f)
        {
            stateX->analogValue = posX;
            addActionToDirtyList(controllerIndex + 1, LA_GunX);
        }

        ActionState *stateY = &gActionStates[controllerIndex + 1][LA_GunY];
        if (fabs(stateY->analogValue - posY) > 0.001f)
        {
            stateY->analogValue = posY;
            addActionToDirtyList(controllerIndex + 1, LA_GunY);
        }

        return; // Event handled
    }
#endif
    switch (e->type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            log_debug("Key: %s, %d", SDL_GetScancodeName(e->key.scancode), e->key.scancode);
            BindingPair *keyPair = &gKeyBindings[e->key.scancode];
            for (int i = 0; i < 2; i++)
            {
                ControlBinding *binding = &keyPair->bindings[i];
                if (binding->type != INPUT_TYPE_NONE)
                {
                    bool isActive = (e->type == SDL_EVENT_KEY_DOWN);
                    updateBindingState(binding, isActive, isActive ? 1.0f : 0.0f);
                }
            }
        }
        break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        {
            log_debug("Joy Button: %s, %d", SDL_GetJoystickNameForID(e->jdevice.which), e->jbutton.button);
            int devIdx = getJoystickID(e->jdevice.which);
            if (devIdx != -1 && e->jbutton.button < MAX_JOY_BUTTONS)
            {
                BindingPair *joyBtnPair = &gJoyButtonBindings[devIdx][e->jbutton.button];
                for (int i = 0; i < 2; i++)
                {
                    ControlBinding *binding = &joyBtnPair->bindings[i];
                    if (binding->type != INPUT_TYPE_NONE)
                    {
                        bool is_active = (e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN);
                        updateBindingState(binding, is_active, is_active ? 1.0f : 0.0f);
                    }
                }
            }
        }
        break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        {
            log_debug("GC Button: %s, %d, %d", SDL_GetGamepadStringForButton(e->gbutton.button), e->gbutton.button, e->gbutton.which);
            int devIdx = getControllerID(e->gbutton.which);
            if (devIdx != -1 && e->gbutton.button < SDL_GAMEPAD_BUTTON_COUNT)
            {
                BindingPair *gcBtnPair = &gControllerButtonBindings[devIdx][e->gbutton.button];
                for (int i = 0; i < 2; i++)
                {
                    ControlBinding *binding = &gcBtnPair->bindings[i];
                    if (binding->type != INPUT_TYPE_NONE)
                    {
                        bool is_active = (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                        updateBindingState(binding, is_active, is_active ? 1.0f : 0.0f);
                    }
                }
            }
        }
        break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        {
            // printf("Jaxis: %d, Value: %d, Device: %d\n", e->jaxis.axis, e->jaxis.value, e->jaxis.which);
            int devIdx = getJoystickID(e->jaxis.which);
            if (devIdx != -1 && e->jaxis.axis < MAX_JOY_AXES)
            {
                BindingPair *pair = &gJoyAxisBindings[devIdx][e->jaxis.axis];
                for (int i = 0; i < 2; i++) // Loop over the pair of possible bindings
                {
                    ControlBinding *binding = &pair->bindings[i];
                    if (binding->type == INPUT_TYPE_NONE)
                        continue;

                    // UPDATED: Get deadzone from the action's properties
                    const int dead_zone = gActionProperties[binding->player][binding->action].deadzone;
                    ActionState *state = &gActionStates[binding->player][binding->action];
                    bool state_did_change = false;
                    float newVal;

                    if (binding->axisMode == AXIS_MODE_DIGITAL)
                    {
                        bool is_active = (abs(e->jaxis.value) > dead_zone)
                                             ? ((binding->axisThreshold > 0) ? (e->jaxis.value > 16384) : (e->jaxis.value < -16384))
                                             : false;

                        if (state->isActive != is_active)
                        {
                            state->isActive = is_active;
                            state_did_change = true;
                        }
                    }
                    else
                    {
                        if (gActionProperties[binding->player][binding->action].isCentering)
                        {
                            newVal = (abs(e->jaxis.value) < dead_zone) ? 0.5f : (float)(e->jaxis.value + 32768) / 65535.0f;
                        }
                        else
                        {
                            // For non-centering axes, the deadzone check is different.
                            // We check if the value has moved sufficiently from its resting point (-32768).
                            if (abs(e->jaxis.value - (-32768)) < dead_zone)
                            {
                                newVal = 0.0f;
                            }
                            else
                            {
                                newVal = (float)(e->jaxis.value + 32768) / 65535.0f;
                            }
                        }

                        if (binding->isInverted)
                            newVal = 1.0f - newVal;

                        if (binding->axisMode == AXIS_MODE_POSITIVE_HALF)
                        {
                            if (fabs(state->positiveContribution - newVal) > 0.001f)
                            {
                                state->positiveContribution = newVal;
                                state_did_change = true;
                            }
                        }
                        else if (binding->axisMode == AXIS_MODE_NEGATIVE_HALF)
                        {
                            if (fabs(state->negativeContribution - newVal) > 0.001f)
                            {
                                state->negativeContribution = newVal;
                                state_did_change = true;
                            }
                        }
                        else
                        { // AXIS_MODE_FULL
                            if (fabs(state->analogValue - newVal) > 0.001f)
                            {
                                state->analogValue = newVal;
                                state_did_change = true;
                            }
                        }
                    }
                    if (state_did_change)
                        addActionToDirtyList(binding->player, binding->action);
                }
            }
        }
        break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        {
            // printf("GC Axis: %s, Value: %d\n", SDL_GameControllerGetStringForAxis(e->caxis.axis), e->caxis.value);
            int devIdx = getControllerID(e->gaxis.which);
            if (devIdx != -1 && e->gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
            {
                BindingPair *pair = &gControllerAxisBindings[devIdx][e->gaxis.axis];
                for (int i = 0; i < 2; i++)
                {
                    ControlBinding *binding = &pair->bindings[i];
                    if (binding->type == INPUT_TYPE_NONE)
                        continue;

                    // UPDATED: Get deadzone from the action's properties
                    const int deadZone = gActionProperties[binding->player][binding->action].deadzone;
                    ActionState *state = &gActionStates[binding->player][binding->action];
                    bool stateDidChange = false;
                    float newVal;

                    if (binding->axisMode == AXIS_MODE_DIGITAL)
                    {
                        bool isActive = (abs(e->gaxis.value) > deadZone)
                                            ? ((binding->axisThreshold > 0) ? (e->gaxis.value > 16384) : (e->gaxis.value < -16384))
                                            : false;
                        if (state->isActive != isActive)
                        {
                            state->isActive = isActive;
                            stateDidChange = true;
                        }
                    }
                    else
                    {
                        bool isTrigger =
                            (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

                        if (gActionProperties[binding->player][binding->action].isCentering && !isTrigger)
                        {
                            newVal = (abs(e->gaxis.value) < deadZone) ? 0.5f : (float)(e->gaxis.value + 32768) / 65535.0f;
                        }
                        else // This path is for triggers and other non-centering axes
                        {
                            newVal = (abs(e->gaxis.value) < deadZone) ? 0.0f : (float)e->gaxis.value / 32767.0f;
                        }

                        if (binding->isInverted)
                            newVal = 1.0f - newVal;

                        if (binding->axisMode == AXIS_MODE_POSITIVE_HALF)
                        {
                            if (fabs(state->positiveContribution - newVal) > 0.001f)
                            {
                                state->positiveContribution = newVal;
                                stateDidChange = true;
                            }
                        }
                        else if (binding->axisMode == AXIS_MODE_NEGATIVE_HALF)
                        {
                            if (fabs(state->negativeContribution - newVal) > 0.001f)
                            {
                                state->negativeContribution = newVal;
                                stateDidChange = true;
                            }
                        }
                        else
                        { // AXIS_MODE_FULL
                            if (fabs(state->analogValue - newVal) > 0.001f)
                            {
                                state->analogValue = newVal;
                                stateDidChange = true;
                            }
                        }
                    }

                    if (stateDidChange)
                        addActionToDirtyList(binding->player, binding->action);
                }
            }
        }
        break;
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
        {
            int devIdx = getJoystickID(e->jhat.which);
            if (devIdx != -1 && e->jhat.hat < MAX_JOY_HATS)
            {
                HatBinding *hat = &gJoyHatBindings[devIdx][e->jhat.hat];

                // An array of the 4 directional bindings for easy iteration
                ControlBinding *directions[] = {&hat->up, &hat->down, &hat->left, &hat->right};

                for (int i = 0; i < 4; i++)
                {
                    ControlBinding *binding = directions[i];
                    if (binding->type == INPUT_TYPE_NONE)
                        continue;

                    // Check if the current hat state activates this direction
                    bool is_active = (e->jhat.value & binding->axisThreshold) != 0;
                    updateBindingState(binding, is_active, is_active ? 1.0f : 0.0f);
                }
            }
        }
        break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (e->button.button < MAX_MOUSE_BUTTONS)
            {
                BindingPair *mouseBtnPair = &gMouseButtonBindings[e->button.button];
                for (int mi = 0; mi < 2; mi++)
                {
                    ControlBinding *binding = &mouseBtnPair->bindings[mi];
                    if (binding->type != INPUT_TYPE_NONE)
                    {
                        bool isActive = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        updateBindingState(binding, isActive, isActive ? 1.0f : 0.0f);
#if defined(PACLOADER_BUILD)
                        addActionToDirtyList(binding->player, binding->action);
#else
                        // Special behavior for shooting games requiring explicit mouse clicks
                        int x, y;
                        if (gId == PRIMEVAL_HUNT_SBPP && getConfig()->emulateTouchscreen &&
                            phIsInsideTouchScreen(e->motion.x, e->motion.y, &x, &y))
                        {
                            phTouchClick(x, y, e->type);
                            phIsDragging = isActive;
                        }
                        else
                        {
                            if (e->button.button == SDL_BUTTON_RIGHT &&
                                (gId == RAMBO_SBQL || gId == RAMBO_SBSS_CHINA || gId == TOO_SPICY_SBMV) && isActive)
                            {
                                ActionState *state = &gActionStates[binding->player][LA_GunX];
                                state->analogValue = -1;
                                addActionToDirtyList(binding->player, LA_GunX);
                                state = &gActionStates[binding->player][LA_GunY];
                                state->analogValue = -1;
                                addActionToDirtyList(binding->player, LA_GunY);
                                gActionStates[binding->player][LA_Reload].isActive = isActive;
                                addActionToDirtyList(binding->player, LA_Reload);
                            }
                            else
                            {
                                addActionToDirtyList(binding->player, binding->action);
                            }
                        }
#endif
                    }
                }
#if !defined(PACLOADER_BUILD)
                if (gameType == MAHJONG && getConfig()->emulateTouchscreen)
                    handleMahjongTouch(e, drawableW, drawableH);
#endif
            }
        }
        break;
        case SDL_EVENT_MOUSE_MOTION:
        {
            float mX = e->motion.x;
            float mY = e->motion.y;
            float posX = 0.0f, posY = 0.0f;

#if !defined(PACLOADER_BUILD)
            if (gId == PRIMEVAL_HUNT_SBPP)
            {
                int x, y;
                if (phIsDragging && phIsInsideTouchScreen(mX, mY, &x, &y))
                    phTouchClick(x, y, e->type);

                int motX = mX, motY = mY;
                phTouchScreenCursor(mX, mY, &motX, &motY);
                posX = ((float)(motX - phX) / (float)phW);
                posY = ((float)(motY - phY) / (float)phH);
            }
            else
#endif
            {
                if (mX <= dest.X)
                    posX = 0;
                else if (mX >= (dest.W + dest.X - 5))
                    posX = dest.W;
                else
                    posX = mX - dest.X;

                if (mY <= dest.Y)
                    posY = 0;
                else if (mY >= (dest.H + dest.Y - 5))
                    posY = dest.H;
                else
                    posY = mY - dest.Y;

                posX /= dest.W;
                posY /= dest.H;
            }

            ControlBinding *bindingX = &gMouseAxisBindings[0]; // X-Axis
            ControlBinding *bindingY = &gMouseAxisBindings[1]; // Y-Axis
            static bool outOfBounds[MAX_PLAYERS + 1] = {false};
            JVSPlayer reloadPlayer = (bindingX->type != INPUT_TYPE_NONE) ? bindingX->player : PLAYER_1;
            if (gameType == SHOOTING && (posX <= 0.01 || posX >= 0.99 || posY <= 0.01 || posY >= 0.99))
            {
                outOfBounds[reloadPlayer] = true;
                if (gId == RAMBO_SBQL || gId == RAMBO_SBSS_CHINA || gId == TOO_SPICY_SBMV)
                {
                    if (bindingX->type != INPUT_TYPE_NONE)
                    {
                        ActionState *state = &gActionStates[bindingX->player][LA_GunX];
                        state->analogValue = -1;
                        addActionToDirtyList(bindingX->player, LA_GunX);
                    }
                    if (bindingY->type != INPUT_TYPE_NONE)
                    {
                        ActionState *state = &gActionStates[bindingY->player][LA_GunY];
                        state->analogValue = -1;
                        addActionToDirtyList(bindingY->player, LA_GunY);
                    }
                }
                gActionStates[reloadPlayer][LA_Reload].isActive = true;
                addActionToDirtyList(reloadPlayer, LA_Reload);
                break;
            }
            else
            {
                if (outOfBounds[reloadPlayer])
                {
                    gActionStates[reloadPlayer][LA_Reload].isActive = false;
                    addActionToDirtyList(reloadPlayer, LA_Reload);
                    outOfBounds[reloadPlayer] = false;
                }
            }

            if (bindingX->type != INPUT_TYPE_NONE)
            {
                ActionState *state = &gActionStates[bindingX->player][bindingX->action];
                if (fabs(state->analogValue - posX) > 0.001f)
                {
                    if (bindingX->isInverted)
                        state->analogValue = 1.0 - posX;
                    else
                        state->analogValue = posX;
                    addActionToDirtyList(bindingX->player, bindingX->action);
                }
            }

            if (bindingY->type != INPUT_TYPE_NONE)
            {
                ActionState *state = &gActionStates[bindingY->player][bindingY->action];
                if (fabs(state->analogValue - posY) > 0.001f)
                {
                    if (bindingY->isInverted)
                        state->analogValue = 1.0 - posY;
                    else
                        state->analogValue = posY;
                    addActionToDirtyList(bindingY->player, bindingY->action);
                }
            }
        }
        break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        {
            Uint32 flags = SDL_GetWindowFlags(g_SdlWindow);
            if (!(flags & SDL_WINDOW_FULLSCREEN) && gId == GHOST_SQUAD_EVOLUTION_SBNJ)
            {
                if (e->type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
                {
                    gActionStates[PLAYER_1][LA_Reload].isActive = true;
                    addActionToDirtyList(PLAYER_1, LA_Reload);
                }
                else // This case must be SDL_EVENT_WINDOW_ENTER
                {
                    gActionStates[PLAYER_1][LA_Reload].isActive = false;
                    addActionToDirtyList(PLAYER_1, LA_Reload);
                }
            }
        }
        break;
    }

    /* Apply digital transitions at once: the frame-end flush would collapse a
     * press and release queued in the same frame into nothing, which cabinet
     * switches such as ES1's TEST would then never see. */
    if (sdlInputInitialized && gNumChangedActions > 0 && !eventIsContinuousAnalog(e))
        processChangedActions();
}

/* Scans one axis binding pair and records which halves it covers. */
static void scan_axis_bindings(BindingPair *pair, bool hasFullAxis[], bool hasHalfSxis[], bool hasPositive[][NUM_LOGICAL_ACTIONS],
                               bool hasNegative[][NUM_LOGICAL_ACTIONS])
{
    for (int k = 0; k < 2; k++)
    {
        ControlBinding *binding = &pair->bindings[k];
        if (binding->type == INPUT_TYPE_NONE)
            continue;

        if (binding->axisMode == AXIS_MODE_FULL)
        {
            hasFullAxis[binding->action] = true;
        }
        else if (binding->axisMode == AXIS_MODE_POSITIVE_HALF)
        {
            hasHalfSxis[binding->action] = true;
            hasPositive[binding->player][binding->action] = true;
        }
        else if (binding->axisMode == AXIS_MODE_NEGATIVE_HALF)
        {
            hasHalfSxis[binding->action] = true;
            hasNegative[binding->player][binding->action] = true;
        }
    }
}

/* Flags actions built from two half-axes, and reports the ones bound to both a
 * full axis and a half-axis, which cannot work. */
void detectCombinedAxes()
{
    gNumCombinedAxes = 0;
    for (int p = 0; p < MAX_ENTITIES; p++)
    {
        for (int i = 0; i < NUM_LOGICAL_ACTIONS; i++)
            gActionProperties[p][i].isCombinedAxis = false;
    }

    // --- Use local variables for detection ---
    bool hasFullAxis[NUM_LOGICAL_ACTIONS] = {false};
    bool hasHalfAxis[NUM_LOGICAL_ACTIONS] = {false};
    bool hasPositive[MAX_ENTITIES][NUM_LOGICAL_ACTIONS] = {false};
    bool hasNegative[MAX_ENTITIES][NUM_LOGICAL_ACTIONS] = {false};

    // --- Pass 1: Scan all axis bindings and flag their types ---
    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        for (int j = 0; j < MAX_JOY_AXES; j++)
        {
            scan_axis_bindings(&gJoyAxisBindings[i][j], hasFullAxis, hasHalfAxis, hasPositive, hasNegative);
        }
        for (int j = 0; j < SDL_GAMEPAD_AXIS_COUNT; j++)
        {
            scan_axis_bindings(&gControllerAxisBindings[i][j], hasFullAxis, hasHalfAxis, hasPositive, hasNegative);
        }
    }

    // --- Pass 2: Check for conflicts and print warnings ---
    for (int i = 0; i < NUM_LOGICAL_ACTIONS; i++)
    {
        if (hasFullAxis[i] && hasHalfAxis[i])
        {
            const char *action_name = "Unknown";
            for (int j = 0; j < NUM_ACTION_NAMES; j++)
            {
                if (gActionNameMap[j].action == i)
                {
                    action_name = gActionNameMap[j].name;
                    break;
                }
            }
            printf("The action '%s' is mapped to both a full axis (e.g., a stick) and half-axes (e.g., triggers).\n", action_name);
            printf("         The half-axis bindings will take priority and may override the full axis input.\n");
        }
    }

    // --- Pass 3: Flag actions that are correctly combined ---
    for (int p = PLAYER_1; p <= MAX_PLAYERS; p++)
    {
        for (int i = 0; i < NUM_LOGICAL_ACTIONS; i++)
        {
            if (hasPositive[p][i] && hasNegative[p][i])
            {
                gActionProperties[p][i].isCombinedAxis = true;
                gCombinedAxes[gNumCombinedAxes++] =
                    (ChangedAction){(JVSPlayer)p, (LogicalAction)i};
            }
        }
    }
}

/* Combines the two half-axis contributions of a combined axis into one value. */
void updateCombinedAxes()
{
    for (int i = 0; i < gNumCombinedAxes; i++)
    {
        const JVSPlayer player = gCombinedAxes[i].player;
        const LogicalAction action = gCombinedAxes[i].action;
        ActionState *state = &gActionStates[player][action];
        // Combine the two halves into a single -1 to 1 value, represented as 0.0 to 1.0
        float finalVal = 0.5f + (state->positiveContribution * 0.5f) - (state->negativeContribution * 0.5f);
        if (finalVal > 1.0f)
            finalVal = 1.0f;
        if (finalVal < 0.0f)
            finalVal = 0.0f;
        if (fabs(state->analogValue - finalVal) > 0.001f)
        {
            state->analogValue = finalVal;
            addActionToDirtyList(player, action);
        }
    }
}

/* Sends every action queued since the last frame to the JVS I/O board. */
void processChangedActions()
{
    for (int i = 0; i < gNumChangedActions; i++)
    {
        JVSPlayer player = gChangedActions[i].player;
        LogicalAction actionId = gChangedActions[i].action;

        // Exit game triggers a clean shutdown via sdlQuit()
        if (actionId == LA_ExitGame && gActionStates[player][actionId].isActive)
        {
            sdlQuit();
            continue;
        }

        if (actionId == LA_CardInsert)
        {
            const CardControlActionResult result =
                cardControlSetInsertState(gActionStates[player][actionId].isActive);
            if (result != CARD_CONTROL_NOT_HANDLED)
            {
                if (result == CARD_CONTROL_HANDLED_ONE_SHOT)
                    gActionStates[player][actionId].isActive = false;
                continue;
            }
        }
        else if (actionId == LA_CardEject && gActionStates[player][actionId].isActive)
        {
            const CardControlActionResult result = cardControlRequestEject();
            if (result != CARD_CONTROL_NOT_HANDLED)
            {
                gActionStates[player][actionId].isActive = false;
                continue;
            }
        }

        JVSActionMapping *map = &gJvsMap[player][actionId];
        ActionState *state = &gActionStates[player][actionId];
        switch (map->call_type)
        {
            case JVS_CALL_SWITCH:
                setSwitch(player, map->jvsInput, state->isActive);
                break;
            case JVS_CALL_KEYPAD:
                setKeypad(map->jvsInput, state->isActive);
                break;
            case JVS_CALL_ANALOGUE:
                // Special handling for digital actions that control an analog JVS input
                switch ((int)actionId)
                {
                    case LA_Gas_Digital:
                    case LA_Brake_Digital:
                        state->analogValue = state->isActive ? 1.0f : 0.0f;
                        break;
                    case LA_Steer_Left:
                    case LA_Flying_Left:
                    case LA_Flying_Up:
                    case LA_Throttle_Slowdown:
                        state->analogValue = state->isActive ? 0.0f : 0.5f;
                        break;
                    case LA_Steer_Right:
                    case LA_Flying_Right:
                    case LA_Flying_Down:
                    case LA_Throttle_Accelerate:
                        state->analogValue = state->isActive ? 1.0f : 0.5f;
                        break;
                }

                if (getConfig()->idSteeringPercentageReduction > 0.0f && map->jvsInput == ANALOGUE_1 &&
                    (gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID4_JAP || gGrp == GROUP_ID5))
                    state->analogValue = (state->analogValue - 0.5f) * (getConfig()->idSteeringPercentageReduction / 100.0f) + 0.5f;

#if !defined(PACLOADER_BUILD)
                if (p1CrossHairInitialized && (map->jvsInput == ANALOGUE_1 || map->jvsInput == ANALOGUE_2))
                {
                    static float lastAnalogue1 = 0.0f;
                    static float lastAnalogue2 = 0.0f;
                    if (map->jvsInput == ANALOGUE_1)
                        lastAnalogue1 = state->analogValue;
                    else
                        lastAnalogue2 = state->analogValue;

                    float xPos = map->jvsInput == ANALOGUE_1 ? state->analogValue : lastAnalogue1;
                    float yPos = map->jvsInput == ANALOGUE_2 ? state->analogValue : lastAnalogue2;

                    updateCrosshairPosition(player - 1, xPos, yPos);
                }

                if (p2CrossHairInitialized && (map->jvsInput == ANALOGUE_3 || map->jvsInput == ANALOGUE_4))
                {
                    static float lastAnalogue3 = 0.0f;
                    static float lastAnalogue4 = 0.0f;
                    if (map->jvsInput == ANALOGUE_3)
                        lastAnalogue3 = state->analogValue;
                    else
                        lastAnalogue4 = state->analogValue;

                    float xPos = map->jvsInput == ANALOGUE_3 ? state->analogValue : lastAnalogue3;
                    float yPos = map->jvsInput == ANALOGUE_4 ? state->analogValue : lastAnalogue4;

                    updateCrosshairPosition(player - 1, xPos, yPos);
                }
#endif

                setAnalogue(map->jvsInput, (int)(state->analogValue * jvsAnalogueMaxValue));
                break;
            case JVS_CALL_COIN:
                if (state->isActive)
                    incrementCoin(player, 1);
                break;
            case JVS_CALL_NONE:
                break;
        }
    }
    for (int i = 0; i < gNumChangedActions; i++)
    {
        const JVSPlayer player = gChangedActions[i].player;
        const LogicalAction action = gChangedActions[i].action;
        gChangedActionFlags[player][action] = false;
    }
    gNumChangedActions = 0; // Clear the list for the next frame
}

/* The WMMT4 cabinet's shifter is an H pattern: every gear closes two of the four
 * shifter switches, which the one action per switch mapping cannot express, so
 * the gear is folded into the switch state here. */
void updateWmmtEs1Shifter()
{
    /* Starts in neutral, which closes none of the shifter switches; the first
     * GearUp selects first and GearDown never goes back below it. */
    static int sequentialGear = 0;
    static bool previousUp = false;
    static bool previousDown = false;

    const bool up = gActionStates[PLAYER_2][LA_GearUp].isActive;
    const bool down = gActionStates[PLAYER_2][LA_GearDown].isActive;
    if (up && !previousUp && sequentialGear < gShifterGears)
        sequentialGear++;
    if (down && !previousDown && sequentialGear > 1)
        sequentialGear--;
    previousUp = up;
    previousDown = down;

    const int directGear = getWmmtDirectGear();
    const int gear = directGear ? directGear : sequentialGear;

    setSwitch(PLAYER_1, BUTTON_5, gear == 1 || gear == 2);
    setSwitch(PLAYER_1, BUTTON_6, gear == 5 || gear == 6);
    setSwitch(PLAYER_1, BUTTON_3, gear == 1 || gear == 3 || gear == 5);
    setSwitch(PLAYER_1, BUTTON_4, gear == 2 || gear == 4 || gear == 6);

    /* The title reads its own cabinet role from this switch rather than a
     * setting, so it follows the configured cabinet mode. */
    setSwitch(PLAYER_1, BUTTON_2,
              getConfig()->namcoES1.cabinetMode == NAMCO_ES1_CABINET_TERMINAL);
}

/* HOD4 gun shake.  The direction threshold is in the game's native resolution
 * so sensitivity does not follow the window size or letterboxing. */
void updateGunShake()
{
    // Screen fraction threshold — a direct fraction of the game's native resolution.
    // Default gShakeMinScreenFraction=0.15 means 15% of screen width/height,
    // which is consistent across any game resolution.
    float thresholdX = (blitWidth > 0) ? gShakeMinScreenFraction : 0.15f;
    float thresholdY = (blitHeight > 0) ? gShakeMinScreenFraction : 0.15f;

    for (int p = PLAYER_1; p <= PLAYER_2; p++)
    {
        // Work in normalized 0.0-1.0 space (already resolution-independent
        // because mouse coords are normalized by dest.W/dest.H in processSdlEvent)
        float normX = gActionStates[p][LA_GunX].analogValue;
        float normY = gActionStates[p][LA_GunY].analogValue;

        float deltaX = normX - gLastGunNormX[p];
        float deltaY = normY - gLastGunNormY[p];

        // Direction detection using game-native-pixel threshold.
        // With default threshold gShakeMinScreenFraction=0.15, the gun must
        // move 15% of the screen width/height before a direction reversal is registered.
        int currentDirX = (deltaX > thresholdX) ? 1 :
                          ((deltaX < -thresholdX) ? -1 : 0);
        int currentDirY = (deltaY > thresholdY) ? 1 :
                          ((deltaY < -thresholdY) ? -1 : 0);

        // Accumulate shake on direction reversal.  Deltas are normalised now,
        // so scale by jvsAnalogueMaxValue to keep ShakeIncreaseRate meaning
        // what it did in JVS units.
        if (currentDirX != 0 && currentDirX == -gLastGunXDir[p])
            gShakeValue[p] += fabsf(deltaX) * gShakeIncreaseRate * jvsAnalogueMaxValue;

        if (currentDirY != 0 && currentDirY == -gLastGunYDir[p])
            gShakeValue[p] += fabsf(deltaY) * gShakeIncreaseRate * jvsAnalogueMaxValue;

        // Apply decay
        gShakeValue[p] *= gShakeDecayRate;
        if (gShakeValue[p] < 1.0f)
            gShakeValue[p] = 0.0f;

        // Clamp the shake value to the max internal range (0-512)
        if (gShakeValue[p] > 512.0f)
            gShakeValue[p] = 512.0f;

        // Map the 0-512 shake value to the final 512-1024 JVS output range
        int jvsOutputValue = 512 + (int)gShakeValue[p];

        // Send the analog values
        if (p == PLAYER_1)
        {
            setAnalogue(ANALOGUE_5, jvsOutputValue);
            setAnalogue(ANALOGUE_6, jvsOutputValue);
        }
        else if (p == PLAYER_2)
        {
            setAnalogue(ANALOGUE_7, jvsOutputValue);
            setAnalogue(ANALOGUE_8, jvsOutputValue);
        }

        // Update last known positions and directions for the next frame
        gLastGunNormX[p] = normX;
        gLastGunNormY[p] = normY;
        if (currentDirX != 0)
            gLastGunXDir[p] = currentDirX;
        if (currentDirY != 0)
            gLastGunYDir[p] = currentDirY;
    }
}

/* Writes the controller GUIDs back to controls.ini when a new pad was assigned. */
void saveGuidsToIni()
{
    if (!gPlayerGUIDsDirty)
    {
        return;
    }

    printf("Saving updated controller GUIDs to controls.ini...\n");
    IniConfig *ini = iniLoad("controls.ini");
    if (!ini)
    {
        // If the file doesn't exist, create an empty config in memory to save.
        ini = (IniConfig *)calloc(1, sizeof(IniConfig));
        if (!ini)
        {
            fprintf(stderr, "ERROR: Failed to allocate memory for INI config.\n");
            return;
        }
    }

    // Set the GUID value for each player. iniSetValue will create the section/key if needed.
    for (int player = 1; player <= MAX_PLAYERS; player++)
    {
        if (strlen(gPlayerGUIDs[player]) > 0)
        {
            char key[16];
            snprintf(key, sizeof(key), "P%d_GUID", player);
            iniSetValue(ini, "ControllerGUIDs", key, gPlayerGUIDs[player]);
        }
    }

    if (iniSave(ini, "controls.ini") == 0)
    {
        printf("Successfully saved controls.ini with updated GUIDs.\n");
    }
    else
    {
        fprintf(stderr, "ERROR: Failed to save controls.ini.\n");
    }

    iniFree(ini);
    gPlayerGUIDsDirty = false;
}

#endif

bool needsPlayer(LogicalAction action, const char *name)
{
    switch ((int)action)
    {
        case LA_Boost:
        case LA_BoostRight:
        case LA_GearUp:
        case LA_GearDown:
        case LA_Gear1:
        case LA_Gear2:
        case LA_Gear3:
        case LA_Gear4:
        case LA_Gear5:
        case LA_Gear6:
        case LA_ViewChange:
        case LA_Intrude:
        case LA_MusicChange:
        case LA_Flying_X:
        case LA_Flying_Y:
        case LA_Flying_Left:
        case LA_Flying_Right:
        case LA_Flying_Up:
        case LA_Flying_Down:
        case LA_Throttle:
        case LA_Throttle_Accelerate:
        case LA_Throttle_Slowdown:
        case LA_GunTrigger:
        case LA_MissileTrigger:
        case LA_ClimaxSwitch:
        case LA_CardInsert:
            return false;
        case LA_Enter:
        case LA_TestUp:
        case LA_TestDown:
        case LA_Nitro:
        case LA_3DChange:
            return strcmp(name, "MaximumHeat3D") != 0;
        case LA_Keypad1:
        case LA_Keypad2:
        case LA_Keypad3:
        case LA_Keypad4:
        case LA_Keypad5:
        case LA_Keypad6:
        case LA_Keypad7:
        case LA_Keypad8:
        case LA_Keypad9:
        case LA_KeypadStar:
        case LA_Keypad0:
        case LA_KeypadHash:
            return strcmp(name, "MaximumHeat3D") != 0;
        case LA_Service:
            return strcmp(name, "MaximumHeat3D") != 0;
        case LA_Steer:
        case LA_Gas:
        case LA_Brake:
            return strcmp(name, "WMMT") != 0 && strcmp(name, "MaximumHeat3D") != 0;
        case LA_Up:
        case LA_Down:
        case LA_Left:
        case LA_Right:
            if (strcmp(name, "Driving") == 0)
                return false;
            else
                return true;
        default:
            if (strcmp(name, "Mahjong") == 0)
                return false;
            else
                return true;
    }
}

/* Renders a binding as "P1_Action" for error messages. */
void getLogicalActionString(const ControlBinding *binding, char *outStr, size_t strSize, const char *name)
{
    const char *actionName = "Unknown";
    for (int i = 0; i < NUM_ACTION_NAMES; i++)
    {
        if (gActionNameMap[i].action == binding->action)
        {
            actionName = gActionNameMap[i].name;
            break;
        }
    }

    if (binding->player == SYSTEM)
    {
        snprintf(outStr, strSize, "%s", actionName);
    }
    else
    {
        if (needsPlayer(binding->action, name))
            snprintf(outStr, strSize, "P%d_%s", binding->player, actionName);
        else
            snprintf(outStr, strSize, "%s", actionName);
    }
}

int listSdlControllers(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    int numJoysticks;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&numJoysticks);
    printf("Found %d controller(s)\n", numJoysticks);

    if (joysticks)
    {
        for (int i = 0; i < numJoysticks; i++)
        {
            SDL_JoystickID instanceId = joysticks[i];
            const char *name = SDL_GetJoystickNameForID(instanceId);
            SDL_GUID guid = SDL_GetJoystickGUIDForID(instanceId);
            char guidStr[33];
            SDL_GUIDToString(guid, guidStr, sizeof(guidStr));

            int count = 0;
            int index = 0;
            for (int j = 0; j < numJoysticks; j++)
            {
                instanceId = joysticks[j];
                const char *otherName = SDL_GetJoystickNameForID(instanceId);
                SDL_GUID other_guid = SDL_GetJoystickGUIDForID(instanceId);
                char other_guid_str[33];
                SDL_GUIDToString(other_guid, other_guid_str, sizeof(other_guid_str));
                if (strcmp(name, otherName) == 0 && strcmp(guidStr, other_guid_str) == 0)
                {
                    if (j < i)
                    {
                        index++;
                    }
                    count++;
                }
            }

            guidStr[4] = '0';
            guidStr[5] = '0';
            guidStr[6] = '0';
            guidStr[7] = '0';
            if (count > 1)
            {
                printf("  - %s %d: %s", name, index, guidStr);
            }
            else
            {
                printf("  - %s: %s", name, guidStr);
            }
            printf("\n");
        }
        SDL_free(joysticks);
    }
    SDL_Quit();
    return 0;
}
