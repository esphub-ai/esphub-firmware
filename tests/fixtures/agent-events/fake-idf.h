/* Minimal host SDK boundary; scheduling and transport live in agent.c. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_INVALID_ARG 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ERROR_CHECK(e) assert((e) == ESP_OK)
const char *esp_err_to_name(esp_err_t e);
size_t strlcpy(char *dest, const char *src, size_t size);

typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
typedef int (*vprintf_like_t)(const char *, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn);
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned EventBits_t;
typedef struct fake_queue *QueueHandle_t;
typedef struct fake_semaphore *SemaphoreHandle_t;
typedef struct fake_event_group *EventGroupHandle_t;
typedef int portMUX_TYPE;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 10
#endif
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 100
#endif
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portYIELD_FROM_ISR() ((void)0)
#define BIT0 1u
#define BIT1 2u
BaseType_t xPortInIsrContext(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *higher);
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
void vQueueDelete(QueueHandle_t q);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                      void *arg, UBaseType_t priority, void *handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
                              BaseType_t clear, BaseType_t all, TickType_t ticks);

#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
void esp_restart(void);
uint32_t esp_get_free_heap_size(void);
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);

typedef enum { ESP_MAC_WIFI_STA, ESP_MAC_ETH } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);
typedef struct { char label[17]; } esp_partition_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_ANY 0
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_boot_partition(void);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p);
typedef struct {
    char project_name[32], version[32], idf_ver[32];
    uint8_t app_elf_sha256[32];
} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *desc);
typedef void *esp_http_client_handle_t;
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(host) ((esp_sntp_config_t){ .server = (host) })
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
#define CONFIG_OKOU_TASK_STACK 4096
#define CONFIG_OKOU_TASK_PRIO 3
#define CONFIG_IDF_TARGET "host"
#define CONFIG_OKOU_BL_PROFILE "host"
