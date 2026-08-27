#ifndef PACLOADER_SDL_FFB_BACKEND_H
#define PACLOADER_SDL_FFB_BACKEND_H

#include <stddef.h>

typedef struct
{
    int enabled;
    int center;
    float springStrength;
    float damperStrength;

    /* Optional shaping values in the 0..1 range. */
    float damperFloor;
    float springDeadband;

    /* Current wheel position, used for constant-force centring fallback. */
    float wheelPosition;
    float constantForce;
    float vibrationStrength;
    int vibrationPeriodMs;
    int vibrationDurationMs;
} FfbSteeringState;

#ifdef __cplusplus
extern "C" {
#endif

void sdlFfbInit(void);
void sdlFfbShutdown(void);
void sdlFfbRumble(float left, float right, int duration_ms);
void sdlFfbApplySteering(const FfbSteeringState *state);
void sdlFfbReapplySteering(void);

/* Poll a quiet source from the FFB worker thread. */
void sdlFfbSetSteeringPoll(void (*poll)(void));
void sdlFfbStopSteering(void);
void sdlFfbDriveboard(const unsigned char *buffer, size_t count);
void sdlFfbOutput(const unsigned char *buffer, size_t count);

#ifdef __cplusplus
}
#endif

#endif
