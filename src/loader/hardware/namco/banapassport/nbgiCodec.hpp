#pragma once

#include <cstddef>
#include <cstdint>

namespace nbgi
{
constexpr size_t HeaderSize = 16;
constexpr uint16_t HeaderVersion = 0x0200;
constexpr uint8_t Nbgic6KeyNumber = 6;

struct DecodedHeader
{
    uint32_t serial;
    uint16_t unknown;
    uint8_t flags;
    uint8_t keyNumber;
};

bool decodeHeader(const uint8_t *header, size_t size, DecodedHeader *decoded);
bool encodeHeader(uint8_t keyNumber, uint32_t serial, uint16_t unknown, uint8_t flags,
                  uint8_t *header, size_t size);
bool encodeAccessCode(uint8_t keyNumber, uint32_t serial, char *accessCode, size_t size);
}

