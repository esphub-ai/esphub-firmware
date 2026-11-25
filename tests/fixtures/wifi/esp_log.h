#pragma once
/* Preserve compile-time use/type checking of log arguments without logging. */
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
