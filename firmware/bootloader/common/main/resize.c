/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bootloader.h"
#include "eh_agent_ext.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_idf_version.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_rom_md5.h"
#if ESP_IDF_VERSION_MAJOR >= 6
#include "esp_private/esp_flash_internal.h"
#else
#include "esp_flash_internal.h"
#endif
#include "mbedtls/base64.h"
#include "nvs.h"

#define SECTOR 4096U
#define APP_ALIGNMENT 65536U
#define SEED_RECORD 128U

static const esp_partition_t *app_slot(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, "app");
}

static const esp_partition_t *filesystem(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "data");
}

static bool current_layout(uint32_t *app_size, uint32_t *data_size)
{
    const esp_partition_t *app = app_slot(), *data = filesystem();
    uint32_t flash_size;
    if (!app || !data || (uint64_t)app->address + app->size != data->address ||
        esp_flash_get_size(NULL, &flash_size) != ESP_OK ||
        (uint64_t)data->address + data->size != flash_size) return false;
    *app_size = app->size;
    *data_size = data->size;
    return true;
}

static bool reboot_pending;

esp_err_t launcher_install(const cJSON *args, cJSON *result, char *error, size_t error_len)
{
    if (reboot_pending) return ESP_ERR_INVALID_STATE;
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(args, "kind"));
    const cJSON *image = cJSON_GetObjectItemCaseSensitive(args, "image_size");
    uint32_t app, data;
    if (kind && !strcmp(kind, "app") && cJSON_IsNumber(image) && current_layout(&app, &data) &&
        image->valuedouble > app) {
        snprintf(error, error_len, "Image %lu bytes, app slot %lu, maximum after resize %lu",
                 (unsigned long)image->valuedouble, (unsigned long)app,
                 (unsigned long)((app + data - 2 * SECTOR) & ~(APP_ALIGNMENT - 1)));
        return ESP_ERR_INVALID_SIZE;
    }
    return eh_install_op(args, result, error, error_len);
}

// Convert only our in-progress marker. The existing OTA event delivery code
// persists a terminal event until the Hub acknowledges it, across reboots.
void reconcile_resize(void)
{
    char *saved = eh_nvs_get_str_alloc(EH_KEY_PENDING);
    if (!saved) return;
    cJSON *marker = cJSON_Parse(saved);
    free(saved);
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(marker, "kind"));
    if (!kind || strcmp(kind, "resize_app")) { cJSON_Delete(marker); return; }
    uint32_t app, data;
    const cJSON *wanted = cJSON_GetObjectItem(marker, "app_size_bytes");
    const cJSON *fs = cJSON_GetObjectItem(marker, "data_size_bytes");
    bool active = current_layout(&app, &data) && cJSON_IsNumber(wanted) && wanted->valuedouble == app &&
                  cJSON_IsNumber(fs) && fs->valuedouble == data;
    if (!active) cJSON_AddStringToObject(marker, "error", "Requested layout is not active");
    cJSON *frame = cJSON_CreateObject();
    if (frame && cJSON_AddItemToObject(frame, "data", marker)) {
        marker = NULL;
        if (cJSON_AddStringToObject(frame, "t", "event") &&
            cJSON_AddStringToObject(frame, "name", active ? "install.done" : "install.failed")) {
            char *encoded = cJSON_PrintUnformatted(frame);
            if (encoded) {
                eh_nvs_set_str(EH_KEY_PENDING, encoded);
                free(encoded);
            }
        }
    }
    cJSON_Delete(frame);
    cJSON_Delete(marker);
}

void report_layout(void)
{
    uint32_t app, data;
    if (!current_layout(&app, &data)) return;
    cJSON *layout = cJSON_CreateObject();
    if (!layout) return;
    cJSON_AddNumberToObject(layout, "app_size_bytes", app);
    cJSON_AddNumberToObject(layout, "data_size_bytes", data);
    eh_event_json("ota.layout", layout);
}

static bool image_fits(const esp_partition_t *app, uint32_t size)
{
    uint32_t header;
    if (esp_partition_read(app, 0, &header, sizeof(header)) != ESP_OK) return false;
    if (header == UINT32_MAX) return true;
    esp_partition_pos_t area = {.offset = app->address, .size = app->size};
    esp_image_metadata_t metadata;
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &area, &metadata) == ESP_OK && metadata.image_len <= size;
}

static esp_err_t primary_table(const esp_partition_t **out)
{
    *out = esp_partition_find_first(ESP_PARTITION_TYPE_PARTITION_TABLE,
                                    ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY, NULL);
    if (*out) return (*out)->address == ESP_PARTITION_TABLE_OFFSET && (*out)->size == SECTOR
                     ? ESP_OK : ESP_ERR_INVALID_STATE;
    return esp_partition_register_external(NULL, ESP_PARTITION_TABLE_OFFSET, SECTOR, "primary",
            ESP_PARTITION_TYPE_PARTITION_TABLE, ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY, out);
}

static esp_err_t change_entries(esp_partition_info_t *rows, const esp_partition_t *app,
                                const esp_partition_t *data, uint32_t size)
{
    int count;
    if (esp_partition_table_verify(rows, false, &count) != ESP_OK || count < 2 ||
        count >= ESP_PARTITION_TABLE_MAX_ENTRIES - 1 || rows[count].magic != ESP_PARTITION_MAGIC_MD5)
        return ESP_ERR_INVALID_STATE;
    esp_partition_info_t *a = &rows[count - 2], *fs = &rows[count - 1];
    if (a->type != app->type || a->subtype != app->subtype || a->pos.offset != app->address ||
        a->pos.size != app->size || strncmp((char *)a->label, "app", sizeof(a->label)) ||
        fs->type != data->type || fs->subtype != data->subtype || fs->pos.offset != data->address ||
        fs->pos.size != data->size || strncmp((char *)fs->label, "data", sizeof(fs->label)))
        return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < count - 2; ++i) {
        if ((uint64_t)rows[i].pos.offset + rows[i].pos.size > app->address) return ESP_ERR_INVALID_STATE;
    }
    a->pos.size = size;
    fs->pos.offset = app->address + size;
    fs->pos.size = data->address + data->size - fs->pos.offset;
    md5_context_t md5;
    esp_rom_md5_init(&md5);
    esp_rom_md5_update(&md5, (uint8_t *)rows, count * sizeof(*rows));
    esp_rom_md5_final((uint8_t *)&rows[count] + ESP_PARTITION_MD5_OFFSET, &md5);
    return esp_partition_table_verify(rows, false, &count);
}

static esp_err_t write_table(const esp_partition_t *table, const void *sector)
{
    // IDF 5.4's esp_ota_begin accepts only app slots. Restrict the write to
    // the verified primary table sector and restore the driver's guard on every exit.
    esp_err_t rc = esp_flash_set_dangerous_write_protection(table->flash_chip, false);
    if (rc != ESP_OK) return rc;
    rc = esp_partition_erase_range(table, 0, SECTOR);
    if (rc == ESP_OK) rc = esp_partition_write(table, 0, sector, SECTOR);
    esp_err_t protect = esp_flash_set_dangerous_write_protection(table->flash_chip, true);
    if (rc != ESP_OK || protect != ESP_OK) return rc != ESP_OK ? rc : protect;
    const void *written;
    esp_partition_mmap_handle_t handle;
    rc = esp_partition_mmap(table, 0, SECTOR, ESP_PARTITION_MMAP_DATA, &written, &handle);
    if (rc == ESP_OK) {
        if (memcmp(written, sector, SECTOR)) rc = ESP_ERR_INVALID_CRC;
        esp_partition_munmap(handle);
    }
    return rc;
}

static esp_err_t apply_resize(const esp_partition_t *app, const esp_partition_t *data,
                              uint32_t size, const uint8_t *seed, const char *pending)
{
    const esp_partition_t *table;
    esp_err_t rc = primary_table(&table);
    if (rc != ESP_OK) return rc;
    esp_partition_info_t *rows = malloc(SECTOR);
    if (!rows) return ESP_ERR_NO_MEM;
    rc = esp_partition_read(table, 0, rows, SECTOR);
    if (rc == ESP_OK) rc = change_entries(rows, app, data, size);
    if (rc == ESP_OK) rc = eh_nvs_set_u8(EH_KEY_HOLD, 1);
    if (rc == ESP_OK) rc = eh_nvs_set_str(EH_KEY_PENDING, pending);
    if (rc == ESP_OK) rc = esp_partition_erase_range(data, 0, data->size);
    if (rc == ESP_OK && size < app->size)
        rc = esp_partition_erase_range(app, size, app->size - size);
    esp_partition_t volume = *data;
    volume.address = app->address + size;
    volume.size = data->address + data->size - volume.address;
    if (rc == ESP_OK) rc = esp_partition_write(&volume, 0, seed, SEED_RECORD);
    if (rc == ESP_OK) rc = esp_partition_write(&volume, SECTOR, seed + SEED_RECORD, SEED_RECORD);
    if (rc == ESP_OK) rc = write_table(table, rows);
    free(rows);
    return rc;
}

esp_err_t resize_app(const cJSON *args, cJSON *result, char *error, size_t error_len)
{
    uint32_t old_size, old_data;
    if (reboot_pending || !current_layout(&old_size, &old_data)) return ESP_ERR_INVALID_STATE;
    const cJSON *requested = cJSON_GetObjectItemCaseSensitive(args, "app_size_bytes");
    uint32_t total = old_size + old_data;
    if (total < APP_ALIGNMENT + 2 * SECTOR || !cJSON_IsNumber(requested) ||
        !(requested->valuedouble >= APP_ALIGNMENT && requested->valuedouble <= total - 2 * SECTOR))
        return ESP_ERR_INVALID_SIZE;
    uint32_t size = (uint32_t)requested->valuedouble;
    if (size != requested->valuedouble || size % APP_ALIGNMENT) return ESP_ERR_INVALID_SIZE;
    uint32_t data_size = total - size;
    cJSON_AddNumberToObject(result, "app_size_bytes", size);
    cJSON_AddNumberToObject(result, "data_size_bytes", data_size);
    if (size == old_size) return ESP_OK;

    const esp_partition_t *app = app_slot(), *data = filesystem();
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(args, "data_size_bytes");
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(args, "erase_littlefs")) ||
        !cJSON_IsNumber(expected) || expected->valuedouble != data_size ||
        data->readonly || app->readonly) return ESP_ERR_INVALID_ARG;
    if (size < old_size && !image_fits(app, size)) {
        strlcpy(error, "Installed app exceeds target or is invalid", error_len);
        return ESP_ERR_INVALID_SIZE;
    }
    const char *encoded = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(args, "empty_fs"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(args, "transfer_id"));
    uint8_t seed[2 * SEED_RECORD];
    size_t decoded;
    if (!encoded || strlen(encoded) != 344 || !id || strlen(id) != 36 ||
        mbedtls_base64_decode(seed, sizeof(seed), &decoded, (const uint8_t *)encoded, 344) ||
        decoded != sizeof(seed)) return ESP_ERR_INVALID_ARG;

    cJSON *marker = cJSON_Duplicate(result, true);
    if (!marker) return ESP_ERR_NO_MEM;
    char *pending = NULL;
    if (cJSON_AddStringToObject(marker, "kind", "resize_app") && cJSON_AddStringToObject(marker, "transfer_id", id))
        pending = cJSON_PrintUnformatted(marker);
    cJSON_Delete(marker);
    if (!pending) return ESP_ERR_NO_MEM;
    // An accepted install owns this NVS key until its terminal receipt is delivered.
    char existing;
    if (eh_nvs_get_str(EH_KEY_PENDING, &existing, sizeof(existing)) != ESP_ERR_NVS_NOT_FOUND) {
        free(pending);
        strlcpy(error, "OTA transfer already pending", error_len);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = apply_resize(app, data, size, seed, pending);
    free(pending);
    if (rc != ESP_OK) {
        eh_nvs_erase(EH_KEY_PENDING);
        return rc;
    }
    reboot_pending = true;
    cJSON_AddBoolToObject(result, "rebooting", true);
    eh_schedule_reboot(1000);
    return ESP_OK;
}
