/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

// Host stand-in for the ESP ROM routine: standard reflected CRC-32 with the
// input register and the result both inverted.
static inline uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1U));
    }
    return ~crc;
}
