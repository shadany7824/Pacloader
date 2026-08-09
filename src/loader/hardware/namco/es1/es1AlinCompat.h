#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int es1AlinDlopen(const char *filename, void **handle);
void *es1AlinDlsym(void *handle, const char *symbol);
int es1AlinDlclose(void *handle);

#ifdef __cplusplus
}
#endif
