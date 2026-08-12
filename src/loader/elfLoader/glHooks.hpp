#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* GLHooks_GetProcAddress(const char* procName);
uint32_t GLHooks_ConsumeCompressedImageSize();
void GLHooks_ResetStateCache();
void GLHooks_NotifyContextCurrent(void *context);
void GLHooks_NotifyTextureBinding(unsigned int target, unsigned int texture);
void GLHooks_NotifyTextureDeleted(int count, const unsigned int *textures);

#ifdef __cplusplus
}
#endif
