#pragma once

#include <SDL3/SDL_keycode.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_PATH_LENGTH 1024
#define INPUT_STRING_LENGTH 256

#define AFTER_BURNER_CLIMAX_SBLR 0xa3c246e2                       // DVP-0009
#define AFTER_BURNER_CLIMAX_SBLR_REVA 0xe657b1c0                  // DVP-0009A
#define AFTER_BURNER_CLIMAX_SBLR_REVB 0xdb9d8396                  // DVP-0009B
#define AFTER_BURNER_CLIMAX_SDX_SBMN 0xc042a0a2                   // DVP-0018
#define AFTER_BURNER_CLIMAX_SDX_SBMN_REVA 0xcb768831              // DVP-0018A
#define AFTER_BURNER_CLIMAX_SE_SBLR 0x86df4e64                    // DVP-0031
#define AFTER_BURNER_CLIMAX_SE_SBLR_REVA 0x684352f4               // DVP-0031A
#define GHOST_SQUAD_EVOLUTION_SBNJ 0x455edaec                     // DVP-0029A
#define HARLEY_DAVIDSON_SBRG 0xb1dd1c12                           // DVP-5007
#define HUMMER_SBQN 0xf3778f44                                    // DVP-0057
#define HUMMER_SBQN_REVB 0x4e6d5c64                               // DVP-0057B
#define HUMMER_EXTREME_SBST 0x7129c32b                            // DVP-0079
#define HUMMER_EXTREME_MDX_SBTL 0xfeaf8484                        // DVP-0083
#define INITIALD_4_SBML_REVA 0x361d1cbe                           // DVP-0019A
#define INITIALD_4_SBML_REVB 0x606914be                           // DVP-0019B
#define INITIALD_4_SBML_REVC 0x50f1c269                           // DVP-0019C
#define INITIALD_4_SBML_REVD 0xba380f8a                           // DVP-0019D
#define INITIALD_4_SBML_REVG 0x6cb19701                           // DVP-0019G
#define INITIALD_4_SBML_REVZ 0xeda16488                           // DVP-0019Z
#define INITIALD_4_SBML_SERVERBOX 0x678f4320                      // DVP-0019
#define INITIALD_4_EXP_SBNK_REVB 0xd0c64f3                        // DVP-0030B
#define INITIALD_4_EXP_SBNK_REVC 0x65ea22e3                       // DVP-0030C
#define INITIALD_4_EXP_SBNK_REVD 0x62cc73a9                       // DVP-0030D
#define INITIALD_5_JAP_SBQZ_REVA 0xb3183112                       // DVP-0070A
#define INITIALD_5_JAP_SBQZ_REVC 0xda04e5e0                       // DVP-0070C
#define INITIALD_5_JAP_SBQZ_REVF 0xfc3dc85d                       // DVP-0070F
#define INITIALD_5_JAP_SBQZ_SERVERBOX 0x5fd379cd                  // DVP-0070A
#define INITIALD_5_EXP_SBRY 0x701b88cf                            // DVP-0075
#define INITIALD_5_EXP20_SBTS 0x77c6b58b                          // DVP-0084
#define INITIALD_5_EXP20_SBTS_REVA 0xd4910e75                     // DVP-0084A
#define LETS_GO_JUNGLE_SBLU 0xb1c8c901                            // DVP-0011
#define LETS_GO_JUNGLE_SBLU_REVA 0xc697c4fb                       // DVP-0011A
#define LETS_GO_JUNGLE_SBNR_SPECIAL 0x240beedc                    // DVP-0036A
#define MJ4_SBPN_REVG 0x57813a7                                   // DVP-0049G
#define MJ4_EVO_SBTA 0xb785a0e9                                   // DVP-0081
#define OUTRUN_2_SP_SDX_SBMB 0x92c196d5                           // DVP-0015
#define OUTRUN_2_SP_SDX_SBMB_REVA 0x4debd5f0                      // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_REVA_TEST 0x6b2d5c46                 // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_REVA_TEST2 0x4ee20716                // DVP-0015A
#define OUTRUN_2_SP_SDX_SBMB_TEST 0xf1c89eae                      // DVP-0015
#define PRIMEVAL_HUNT_SBPP 0x6868215c                             // DVP-0048A
#define QUIZ_AXA_SBMS 0x434dee4                                   // DVP-0025H
#define QUIZ_AXA_SBUR_LIVE 0x7103fafc                             // DVP-0087D
#define RAMBO_SBQL 0x48f49dd                                      // DVP-0069
#define RAMBO_SBSS_CHINA 0xad864fec                               // DVP-0078
#define R_TUNED_SBQW 0xa68d053d                                   // DVP-0060
#define SEGABOOT 0x0027                                           // DVP-????
#define SEGABOOT_2_4 0x38d56318                                   // DVP-????
#define SEGABOOT_2_4_SYM 0xa518b18b                               // DVP-????
#define SEGABOOT_2_6 0x0030                                       // DVP-????
#define SEGA_RACE_TV_SBPF 0xb30ab16a                              // DVP-0044
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA 0x226281ad              // DVP-0003A
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA_TEST 0x1d1160aa         // DVP-0003A
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB 0xad652b5               // DVP-0003B
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB_TEST 0xbb24fef8         // DVP-0003B
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC 0xe213414d              // DVP-0003C
#define THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC_TEST 0x6210582b         // DVP-0003C
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS 0x8a7c4ac7           // DVP-0010
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_TEST 0xb896a44a      // DVP-0010
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB 0x7d6ab3e3      // DVP-0010B
#define THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB_TEST 0xae47a9fc // DVP-0010B
#define THE_HOUSE_OF_THE_DEAD_EX_SBRC 0x20115a92                  // DVP-0063
#define THE_HOUSE_OF_THE_DEAD_EX_SBRC_TEST 0xd58a0053             // DVP-0063
#define TOO_SPICY_SBMV 0x46bb306e                                 // DVP-0027A
#define TOO_SPICY_SBMV_TEST 0x6510b2e6                            // DVP-0027A
#define UNKNOWN 0xFFFFFFFF                                        // ????????
#define VIRTUA_FIGHTER_5_SBLM 0x71722584                          // DVP-0008
#define VIRTUA_FIGHTER_5_SBQU_REVA 0x9745abb6                     // DVP-0008A
#define VIRTUA_FIGHTER_5_SBQU_REVB 0x8953bd52                     // DVP-0008B
#define VIRTUA_FIGHTER_5_SBQU_REVE 0x4c2edbf6                     // DVP-0008E
#define VIRTUA_FIGHTER_5_SBLM_EXPORT 0xec474630                   // DVP-0043
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBUV_REVA 0xbae2be62      // DVP-5019A
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB 0x7cee1d81      // DVP-5020
#define VIRTUA_FIGHTER_5_FINAL_SHOWDOWN_SBXX_REVB_6000 0x34c0d02  // DVP-5020 ver 6.00 (Weird public version)
#define VIRTUA_FIGHTER_5_SBQU_R 0x79db39d                         // DVP-5004
#define VIRTUA_FIGHTER_5_SBQU_R_REVD 0x443b6d07                   // DVP-5004D
#define VIRTUA_FIGHTER_5_SBQU_R_REVG 0x4702ae73                   // DVP-5004G
#define VIRTUA_TENNIS_3_SBKX 0x7a021b5                                 // DVP-0005
#define VIRTUA_TENNIS_3_SBKX_TEST 0x3bfbd11e                           // DVP-0005
#define VIRTUA_TENNIS_3_SBKX_REVA 0xa9a10e32                           // DVP-0005A
#define VIRTUA_TENNIS_3_SBKX_REVA_TEST 0xdbcf31c1                      // DVP-0005A
#define VIRTUA_TENNIS_3_SBKX_REVB 0x67776c01                           // DVP-0005B
#define VIRTUA_TENNIS_3_SBKX_REVB_TEST 0xc689beeb                      // DVP-0005B
#define VIRTUA_TENNIS_3_SBKX_REVC 0x55345ed0                           // DVP-0005C
#define VIRTUA_TENNIS_3_SBKX_REVC_TEST 0xdd7c4fea                      // DVP-0005C

typedef enum
{
    WORKING,
    NOT_WORKING
} GameStatus;

typedef enum
{
    NOREGION = -1,
    JP = 0,
    US = 1,
    EX = 2
} GameRegion;

typedef enum
{
    SHOOTING,
    DRIVING,
    DIGITAL,
    FLYING,
    MAHJONG
} GameType;

typedef enum
{
    GROUP_UNKNOWN,
    GROUP_WMMT3,
    GROUP_MAXIMUM_HEAT_3D,
    GROUP_WMMT4_ES1,
    GROUP_KIZUNA_ES1,
    GROUP_ABC,
    GROUP_HOD4,
    GROUP_HOD4_TEST,
    GROUP_HOD4_SP,
    GROUP_HOD4_SP_TEST,
    GROUP_HUMMER,
    GROUP_ID4_EXP,
    GROUP_ID4_JAP,
    GROUP_ID5,
    GROUP_ID_SERVERBOX,
    GROUP_LGJ,
    GROUP_OUTRUN,
    GROUP_OUTRUN_TEST,
    GROUP_RAMBO,
    GROUP_VF5,
    GROUP_VT3,
    GROUP_VT3_TEST
} GameGroup;

typedef enum
{
    ARCADE_PLATFORM_NAMCO_N2,
    ARCADE_PLATFORM_NAMCO_ES1
} ArcadePlatform;

typedef enum
{
    AUTO_DETECT_GPU,
    NVIDIA_GPU,
    AMD_GPU,
    ATI_GPU,
    INTEL_GPU,
    UNKNOWN_GPU,
    ERROR_GPU
} GpuType;

typedef struct
{
    unsigned int service;
    unsigned int start;
    unsigned int coin;
    unsigned int up;
    unsigned int down;
    unsigned int left;
    unsigned int right;
    unsigned int button1;
    unsigned int button2;
    unsigned int button3;
    unsigned int button4;
    unsigned int button5;
    unsigned int button6;
    unsigned int button7;
    unsigned int button8;
} PlayerKeyMapping;

typedef struct
{
    unsigned int test;
    PlayerKeyMapping player1;
    PlayerKeyMapping player2;
} KeyMapping;

typedef enum
{
    NAMCO_N2_TYPE,
    NAMCO_ES1_TYPE
} JVSIOType;


typedef struct
{
    int minimum;
    int maximum;
} NamcoN2AxisRange;

typedef struct
{
    char name[128];
    int players;
    int switches;
    int coins;
    int analogueInputs;
    int rotaryInputs;
    int generalPurposeOutputs;
    int analogueOutputs;
    int generalPurposeInputs;
} NamcoN2JvsConfig;

/* Settings for an external YaCardEmu.  The same Sanwa reader appears on N2 and
 * on the ES1 terminal, so the shape is shared but the values are per-platform. */
typedef struct
{
    int enabled;
    int autoStart;
    int diagnostics;
    int apiPort;
    char executablePath[MAX_PATH_LENGTH];
    char pipeName[MAX_PATH_LENGTH];
    char apiHost[MAX_PATH_LENGTH];
    char cardName[MAX_PATH_LENGTH];
} YaCardEmuConfig;

typedef struct
{
    /* Where the Banapassport reader keeps its own settings and card. Blank
     * takes the default name. */
    char cardFile[MAX_PATH_LENGTH];
} NamcoES1IcCardConfig;

typedef struct
{
    int enabled;
    int autoInsert;
    int diagnostics;
    char cardFile[MAX_PATH_LENGTH];
} NamcoES1LegacyCardConfig;

typedef YaCardEmuConfig NamcoN2CardConfig;

typedef struct
{
    int enabled;
    char interfaceName[128];
    char bindAddress[16];
    char broadcastAddress[16];
    int allowBroadcast;
    int rewriteBroadcast;
} NamcoN2NetworkConfig;

typedef struct
{
    char dongleId[13];
    char dongleId2[13];
    int debugMode;
    int forceFeedbackEnabled;
    int forceFeedbackDiagnostics;

    /* Force feedback tuning, percentages except the reflection range: a race
     * swings about thirty five of reflection's 127 counts, so the range says
     * what counts as full deflection.  Lower means heavier. */
    int ffbGain;
    int ffbSpringGain;
    int ffbDamperGain;
    int ffbVibrationGain;
    int ffbReflectRange;
    int ffbInvert;

    /* Feel rather than strength, off by default.  The cabinet is direct drive,
     * so the game's own viscosity is the damping it intends; a floor stops a
     * wheel going slack, a deadband stops a spring hunting at centre. */
    int ffbDamperFloor;
    int ffbSpringDeadband;
    /* Raw counts the potentiometers report at each end of travel.  The test
     * menu shows these divided by 64, so a wheel reading +/-512 there swings
     * the full 16-bit range.  The pedals rest at their minimum. */
    NamcoN2AxisRange steering;
    NamcoN2AxisRange accelerator;
    NamcoN2AxisRange brake;
    /* What the loader's JVS I/O board reports about itself.  The counts are the
     * ones initSystemN2() asks for; lowering one is only useful for working out
     * what a cabinet actually needs. */
    NamcoN2JvsConfig jvs;
    NamcoN2CardConfig card;
    NamcoN2NetworkConfig network;
} NamcoN2Config;

/* System ES1 is not an N2 variant: separate knobs, so an ES1 launch can never
 * inherit N2's card, FFB, network or JVS settings. */
typedef enum
{
    NAMCO_ES1_CABINET_DRIVE = 0,
    NAMCO_ES1_CABINET_TERMINAL = 1,
} NamcoES1CabinetMode;

typedef struct
{
    int cameraEnabled;
    int dongleEnabled;
    int serialDiagnostics;
    int emulateJamma;
    /* The cabinet's steering force, taken from the title's own FFB object. */
    int forceFeedbackEnabled;
    int forceFeedbackDiagnostics;
    /* WMMT6/OpenParrot-compatible FFB tuning, in profile percentages. */
    int ffbGain;
    int ffbWeight;
    int ffbSpringGain;
    int ffbDamperGain;
    int ffbVibrationGain;
    int ffbBaseDamper;
    int ffbDeadband;
    int ffbRumbleDuration;
    int ffbInvert;
    NamcoES1CabinetMode cabinetMode;
    /* HASP dongle serial, twelve digits. The title's ALL.Net serial is "ABEN"
     * plus the last seven, which is what a server keys the shop off. */
    char haspSerial[16];
    /* Let a WMMT4 drive cabinet boot without a separate terminal process. */
    int terminalEmulatorEnabled;
    /* Optional WMMT4 DNS overrides. Empty strings use normal host DNS. */
    char dnsNbgiLoc[MAX_PATH_LENGTH];
    char dnsTenporouterLoc[MAX_PATH_LENGTH];
    char dnsBbrouterLoc[MAX_PATH_LENGTH];
    char dnsMuchaLocal[MAX_PATH_LENGTH];
    char dnsNaominetJp[MAX_PATH_LENGTH];
    NamcoES1IcCardConfig icCard;
    NamcoES1LegacyCardConfig legacyCard;
    /* The terminal cabinet carries a magnetic card reader for transferring
     * WMMT3DX+ cards; the drive cabinet has none. */
    YaCardEmuConfig card;
} NamcoES1Config;

typedef struct
{
    ArcadePlatform platform;
    NamcoN2Config namcoN2;
    NamcoES1Config namcoES1;
    int emulateRideboard;
    int emulateDriveboard;
    int emulateMotionboard;
    int emulateHW210CardReader;
    int emulateIDCardReader;
    int emulateTouchscreen;
    int emulateJVS;
    int fullscreen;
    int width;
    int height;
    int boostRenderRes;
    GameStatus gameStatus;
    GameType gameType;
    GameGroup gameGroup;
    KeyMapping keymap;
    uint32_t crc32;
    GameRegion region;
    int freeplay;
    int showDebugMessages;
    int useAltJvsPassthrough;
    char *gameID;
    char *gameTitle;
    char *gameShortTitle;
    char *gameDVP;
    char *gameReleaseYear;
    char *gameNativeResolutions;
    GpuType GPUVendor;
    char *GPUVendorString;
    JVSIOType jvsIOType;
    int hummerFlickerFix;
    int keepAspectRatio;
    int fpsLimiter;
    float fpsTarget;
    /* Present on the display's refresh instead of the limiter's QPC grid, which
     * stops tearing. Off by default: it also stops the limiter pacing, so every
     * present blocks for a blank - which crawls through the loads a title does
     * while presenting. Worth it on WMMT4, measured a regression on WMMT3. */
    int vsync;
    int lgjRenderWithMesa;
    float idSteeringPercentageReduction;
    int disableBuiltinFont;
    int disableBuiltinLogos;
    int hideCursor;
    int mj4EnabledAtT;
    int inputMode; // 0 = both, 1 = SDL, 2 = EVDEV only
    float whiteBorderPercentage;
    float blackBorderPercentage;
    int borderEnabled;
} EmulatorConfig;

#ifdef __cplusplus
extern "C" {
#endif

int initConfig(const char *configFilePath);
void setDefaultValues(EmulatorConfig *cfg);
EmulatorConfig *getConfig();
char *getGameName();
char *getDvpName();
char *getGameId();
char *getGameReleaseYear();
char *getGameNativeResolutions();
const char *getGameRegionString(GameRegion region);
const char *getGpuTypeString(GpuType gpuType);
extern SDL_Keycode getSDLKeycode(const char *input);

#ifdef __cplusplus
}
#endif
