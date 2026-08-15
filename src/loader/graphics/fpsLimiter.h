#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void initFpsLimiter(void);
int64_t clockNow(void);
double calculateFps(void);
void frameTiming(void);
uint64_t videoSyncCount(void);
void waitVideoSync(uint64_t index);
int consumeVideoSyncWait(void);
/* The window whose monitor supplies the vertical blank clock. Set before
 * initFpsLimiter() or the QPC grid is used instead. */
void setVideoSyncWindow(void *window);

#ifdef __cplusplus
}
#endif
