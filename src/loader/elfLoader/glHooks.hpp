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
void GLHooks_NotifyTextureBinding(unsigned int target, unsigned int texture);
void GLHooks_NotifyTextureDeleted(int count, const unsigned int *textures);

#ifdef __cplusplus
}
#endif
