/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <string.h>
#include "esp_rom_crc.h"

#define LITTLEFS_ROOT_RECORD 128U

// Root commit that LittleFS 2.1 lfs_format writes to blocks 0 and 1 with 4 KiB
// erase blocks, 128-byte programs and 63-byte names. Only the revision, the
// block count and the CRC differ between volumes.
static const uint8_t littlefs_root_commit[60] = {
    0x01, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0xff, 0xf7, 0x6c, 0x69, 0x74, 0x74,
    0x6c, 0x65, 0x66, 0x73, 0x2f, 0xe0, 0x00, 0x10, 0x01, 0x00, 0x02, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0x7f, 0xfe, 0x03, 0x00, 0x00, 0x7f, 0xef, 0xfc, 0x10,
    0x80, 0x00, 0x00, 0x00, 0xb3, 0xab, 0xd2, 0x9a, 0x0f, 0xf0, 0x00, 0x4c,
};

// Fill one program-sized record of an empty volume. The rest of each 4 KiB
// block stays erased. Revision 1 belongs to block 0 and revision 2 to block 1.
static inline void littlefs_empty_root(uint8_t record[LITTLEFS_ROOT_RECORD], uint32_t revision,
                                       uint32_t block_count)
{
    memset(record, 0xff, LITTLEFS_ROOT_RECORD);
    memcpy(record, littlefs_root_commit, sizeof(littlefs_root_commit));
    memcpy(record, &revision, sizeof(revision));
    memcpy(record + 28, &block_count, sizeof(block_count));
    // LittleFS stores the CRC-32 register without the final inversion that the ROM applies.
    uint32_t crc = ~esp_rom_crc32_le(0, record, sizeof(littlefs_root_commit));
    memcpy(record + sizeof(littlefs_root_commit), &crc, sizeof(crc));
}
