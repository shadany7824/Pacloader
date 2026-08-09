#pragma once

/*
 * N2 display manager and render bridge: the adm* entry points on the loader's
 * SDL/GL context, plus the hooks that keep Alchemy's uploads on the main
 * thread.  The three public entry points stay declared in n2.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Routes Alchemy's texture uploads onto the loader's main thread; returns the
// number of hooks installed.
int n2InstallTextureDispatchHooks(void);

// Redirects Alchemy's meta-type, arena and image allocation entry points.
void n2InstallAlchemyImageHooks(void);

#ifdef __cplusplus
}
#endif
