#pragma once
#include <stddef.h>
/* ESP-IDF supplies strlcpy; the host libc need not. */
size_t strlcpy(char *dest, const char *src, size_t size);
