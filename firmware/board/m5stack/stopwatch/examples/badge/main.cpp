// Six image slots, adapted from M5Stack's M5StopWatch-UserDemo/app_badge (MIT).
#include "../common/round_display.hpp"
#include "cJSON.h"
#include "eh_agent.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t max_bytes = 256 * 1024;
constexpr int slots = 6;
constexpr char staging[] = "/badges/sw-badge.tmp";

bool mount_storage()
{
    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = "/badges";
    conf.partition_label = "data";
    if (esp_vfs_littlefs_register(&conf) == ESP_OK) return true;

    // A new USB setup leaves this shared partition erased. Never reformat a
    // populated filesystem if its mount failed.
    const auto *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                 ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "data");
    if (!part) return false;
    uint8_t page[1024];
    for (size_t i = 0; i < part->size; i += sizeof(page)) {
        if (esp_partition_read(part, i, page, sizeof(page)) != ESP_OK ||
            !std::all_of(page, page + sizeof(page), [](uint8_t b) { return b == 0xff; }))
            return false;
    }
    conf.format_if_mount_failed = true;
    return esp_vfs_littlefs_register(&conf) == ESP_OK;
}

bool draw_image(const uint8_t *data, size_t len)
{
    auto &d = M5.Display;
    const int x = d.width() / 2, y = d.height() / 2;
    if (len >= 2 && data[0] == 0xff && data[1] == 0xd8)
        return d.drawJpg(data, len, x, y, d.width(), d.height(), 0, 0, 1, 1, lgfx::datum_t::middle_center);
    if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0)
        return d.drawPng(data, len, x, y, d.width(), d.height(), 0, 0, 1, 1, lgfx::datum_t::middle_center);
    return false;
}

void slot_path(int slot, char (&path)[32])
{
    snprintf(path, sizeof(path), "/badges/sw-badge-%d.img", slot);
}

void show(int slot, uint8_t *image)
{
    char path[32];
    slot_path(slot, path);
    M5.Display.clear();
    if (FILE *f = fopen(path, "rb")) {
        const size_t size = fread(image, 1, max_bytes, f);
        const bool complete = !ferror(f) && fgetc(f) == EOF;
        fclose(f);
        if (complete && draw_image(image, size)) return;
    }
    char label[24];
    snprintf(label, sizeof(label), "Badge %d / %d", slot, slots);
    centered(label, 185, 4, TFT_CYAN);
    centered("Send an image via EspHub", 245, 2);
    centered("A: previous    B: next", 300, 2);
}

size_t download(const char *url, uint8_t *image)
{
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 5000;
    cfg.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return 0;
    size_t size = 0;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const int64_t length = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) == 200 && length <= static_cast<int64_t>(max_bytes)) {
            const int64_t end = esp_timer_get_time() + 30000000;
            while (size < max_bytes && esp_timer_get_time() < end) {
                const int n = esp_http_client_read(client, reinterpret_cast<char *>(image + size),
                                                   std::min(max_bytes - size, size_t{4096}));
                if (n <= 0) break;
                size += n;
            }
            if (!esp_http_client_is_complete_data_received(client)) size = 0;
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return size;
}

bool update_slot(const cJSON *payload, uint8_t *image, int &slot)
{
    const auto *number = cJSON_GetObjectItemCaseSensitive(payload, "slot");
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(payload, "url"));
    if (!cJSON_IsNumber(number) || number->valueint != number->valuedouble ||
        number->valueint < 1 || number->valueint > slots || !url || strncmp(url, "https://", 8))
        return false;
    const size_t size = download(url, image);
    if (!size || !draw_image(image, size)) return false;

    FILE *f = fopen(staging, "wb");
    if (!f) return false;
    const bool written = fwrite(image, 1, size, f) == size;
    const bool closed = fclose(f) == 0;
    char path[32];
    slot_path(number->valueint, path);
    if (!written || !closed || rename(staging, path) != 0) {
        remove(staging);
        return false;
    }
    slot = number->valueint;
    return true;
}
} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    auto *image = static_cast<uint8_t *>(heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!image || !mount_storage()) {
        heap_caps_free(image);
        centered("Badge storage unavailable", 230, 2);
        return;
    }
    int slot = 1;
    show(slot, image);
    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
            slot = (slot - 1 + (M5.BtnA.wasPressed() ? slots - 1 : 1)) % slots + 1;
            show(slot, image);
        }
        if (char *raw = eh_message_receive()) {
            cJSON *message = cJSON_Parse(raw);
            free(raw);
            const bool ok = update_slot(cJSON_GetObjectItemCaseSensitive(message, "payload"), image, slot);
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(message, "id"));
            eh_message_send(ok ? "{\"saved\":true}" : "{\"saved\":false}", id);
            cJSON_Delete(message);
            show(slot, image);
        }
        M5.delay(15);
    }
}
