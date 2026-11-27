/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "littlefs_root.h"

// Print the two root records for every 64 KiB capacity the Launcher can create.
int main(void)
{
    uint8_t pair[2 * LITTLEFS_ROOT_RECORD];
    for (uint32_t size = 65536; size <= 30801920; size += 65536) {
        littlefs_empty_root(pair, 1, size / 4096);
        littlefs_empty_root(pair + LITTLEFS_ROOT_RECORD, 2, size / 4096);
        if (fwrite(pair, sizeof(pair), 1, stdout) != 1) return 1;
    }
    return 0;
}
