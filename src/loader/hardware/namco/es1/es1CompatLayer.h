#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/* Common primitives shared by title-specific ES1 adapters. */
struct Es1HookSpec
{
    uintptr_t address;
    void *replacement;
    const char *name;
    void **original = nullptr;
};

int es1InstallHookTable(const Es1HookSpec *hooks, size_t count, const char *title);

uint32_t es1CompatCrc32Mpeg(const uint8_t *data, size_t length);
int es1CompatReadBlob(const uint8_t *blob, size_t blobSize, int offset, int length,
                      uint8_t *buffer);
int es1CompatWriteBlob(uint8_t *blob, size_t blobSize, int offset, int length,
                       const uint8_t *buffer);
bool es1CompatGzipDecompress(const uint8_t *data, size_t size,
                             std::vector<uint8_t> &output);

