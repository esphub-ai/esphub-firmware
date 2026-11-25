/* Volatile, remotely enabled console redirection. SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "okou_priv.h"
#include "okou_iot.h"

#define RING_BYTES 2048
#define FLUSH_US 250000

typedef struct {
    char ring[RING_BYTES];
    size_t head, used;
    uint32_t dropped;
    int64_t flush_at;
    // Only the agent uses these; no per-publication heap or large agent stack frames.
    uint8_t batch[OKOU_LOG_CHUNK_BYTES];
    char wire[OKOU_LOG_WIRE_BYTES];
} console_t;

// Only the agent enables, drains and frees the capture. Producers borrow it
// under the lock; a disabled Python stdout write only checks the atomic pointer.
static _Atomic(console_t *) capture;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static _Atomic(vprintf_like_t) native_print = vprintf;
static _Atomic(TaskHandle_t) publishing_task;

static void count_loss(console_t *c, size_t bytes)
{
    c->dropped += bytes > UINT32_MAX - c->dropped ? UINT32_MAX - c->dropped : (uint32_t)bytes;
}

static bool append(const char *data, size_t len, size_t truncated)
{
    if (!capture || xPortInIsrContext()) return false;
    bool wake = false;
    portENTER_CRITICAL(&lock);
    console_t *c = capture;
    if (c && len) {
        wake = c->used == 0 || c->flush_at == INT64_MAX;
        if (len > RING_BYTES) {
            size_t skip = len - RING_BYTES;
            count_loss(c, skip);
            data += skip;
            len = RING_BYTES;
        }
        size_t space = RING_BYTES - c->used;
        if (len > space) {
            size_t evict = len - space;
            c->head = (c->head + evict) % RING_BYTES;
            c->used -= evict;
            count_loss(c, evict);
        }
        size_t tail = (c->head + c->used) % RING_BYTES;
        size_t first = len < RING_BYTES - tail ? len : RING_BYTES - tail;
        memcpy(c->ring + tail, data, first);
        memcpy(c->ring, data + first, len - first);
        c->used += len;
        count_loss(c, truncated);
        if (wake) c->flush_at = esp_timer_get_time() + FLUSH_US;
    }
    bool redirected = c != NULL;
    portEXIT_CRITICAL(&lock);
    if (wake) okou_agent_notify();
    return redirected;
}

bool okou_console_write(const char *data, size_t len)
{
    return append(data, len, 0);
}

static int console_vprintf(const char *format, va_list args)
{
    // The TLS/MQTT write can itself emit ESP_LOGx on this task. Sending that
    // output back through MQTT would keep an otherwise idle device publishing.
    if (capture && !xPortInIsrContext() && publishing_task != xTaskGetCurrentTaskHandle()) {
        char text[256];
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(text, sizeof(text), format, copy);
        va_end(copy);
        if (n < 0) return n;
        size_t len = (size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1;
        if (append(text, len, (size_t)n - len)) return n;
    }
    return native_print(format, args);
}

esp_err_t okou_console_set(bool enabled)
{
    console_t *c = capture;
    if (enabled == (c != NULL)) return ESP_OK;
    if (enabled) {
        c = okou_malloc_large(sizeof(*c));
        if (!c) return ESP_ERR_NO_MEM;
        memset(c, 0, sizeof(*c));
        capture = c;
        native_print = esp_log_set_vprintf(console_vprintf);
    } else {
        esp_log_set_vprintf(native_print);
        portENTER_CRITICAL(&lock);
        capture = NULL;
        portEXIT_CRITICAL(&lock);
        free(c);
    }
    return ESP_OK;
}

int64_t okou_console_deadline(void)
{
    if (!capture) return INT64_MAX;
    portENTER_CRITICAL(&lock);
    console_t *c = capture;
    int64_t deadline = c->used ? c->flush_at : INT64_MAX;
    portEXIT_CRITICAL(&lock);
    return deadline;
}

void okou_console_flush(void)
{
    console_t *c = capture;
    if (!c) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (!c->used || c->flush_at > now) {
        portEXIT_CRITICAL(&lock);
        return;
    }
    size_t len = c->used < sizeof(c->batch) ? c->used : sizeof(c->batch);
    // A stdout writer can split even a single character across calls. Keep an
    // incomplete UTF-8 suffix for the next batch without polling an idle VM.
    size_t lead = len - 1;
    while (lead && len - lead < 4 &&
           ((uint8_t)c->ring[(c->head + lead) % RING_BYTES] & 0xc0) == 0x80) --lead;
    uint8_t ch = c->ring[(c->head + lead) % RING_BYTES];
    size_t width = 1;
    if (ch >= 0xc2 && ch <= 0xdf) width = 2;
    if (ch >= 0xe0 && ch <= 0xef) width = 3;
    if (ch >= 0xf0 && ch <= 0xf4) width = 4;
    if (len - lead < width) len = lead;
    if (!len) {
        c->flush_at = INT64_MAX;
        portEXIT_CRITICAL(&lock);
        return;
    }
    size_t first = len < RING_BYTES - c->head ? len : RING_BYTES - c->head;
    memcpy(c->batch, c->ring + c->head, first);
    memcpy(c->batch + first, c->ring, len - first);
    c->head = (c->head + len) % RING_BYTES;
    c->used -= len;
    uint32_t dropped = c->dropped;
    c->dropped = 0;
    c->flush_at = now + FLUSH_US;
    portEXIT_CRITICAL(&lock);

    publishing_task = xTaskGetCurrentTaskHandle();
    bool sent = okou_iot_publish_log(c->batch, len, dropped, c->wire);
    publishing_task = NULL;
    if (!sent) {
        portENTER_CRITICAL(&lock);
        count_loss(c, dropped);
        count_loss(c, len);
        portEXIT_CRITICAL(&lock);
    }
}
