#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* GLHooks_GetProcAddress(const char* procName);
uint32_t GLHooks_ConsumeCompressedImageSize();
void GLHooks_ResetStateCache();
void GLHooks_NotifyContextCurrent(void *context);
/* Call from the thread that owns the window, whenever the drawable size is
 * established or changes. The GL hooks letterbox the guest's viewport onto it. */
void GLHooks_SetDrawableSize(int drawableWidth, int drawableHeight);
/* Records the drawable size without touching letterboxing, for the titles that
 * do not letterbox at all. Owning thread only. */
void GLHooks_PublishDrawableSize(int drawableWidth, int drawableHeight);
/* Reads the published size back. Safe from any thread, unlike asking SDL; zero
 * when nothing has been published yet, leaving the outputs untouched. */
int GLHooks_GetDrawableSize(int *drawableWidth, int *drawableHeight);
/* Non-zero while the guest draws at its own size and present scales it up. */
int GLHooks_NativeUpscaleActive(void);
/* Feeds the hooks' shadow of the guest's enable bits from bridgeglEnable /
 * bridgeglDisable, which is where the guest's glEnable actually lands. */
void GLHooks_NotifyCapToggled(unsigned int cap, int enabled);
void GLHooks_NotifyTextureBinding(unsigned int target, unsigned int texture);
void GLHooks_NotifyTextureDeleted(int count, const unsigned int *textures);

#ifdef __cplusplus
}
#endif
