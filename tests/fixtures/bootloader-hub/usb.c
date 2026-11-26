#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eh_agent_ext.h"
#include "eh_ble.h"
#include "bootloader.h"

static unsigned writes;
static char stored_hub[EH_HUB_HOST_MAX + 1];

esp_err_t eh_nvs_set_str(const char *key, const char *value)
{
    ++writes;
    if (!strcmp(key, "hub")) snprintf(stored_hub, sizeof(stored_hub), "%s", value);
    return ESP_OK;
}
esp_err_t eh_nvs_get_str(const char *key, char *value, size_t len) { return -1; }
esp_err_t eh_nvs_erase(const char *key) { ++writes; return ESP_OK; }
esp_err_t eh_ble_install(const char *hex) { ++writes; return ESP_OK; }

int main(void)
{
    const char *invalid[] = {"", "wss://api.esphub.ai", "https://api.esphub.ai", "http://hub/",
        "api.esphub.ai/", "api.esphub.ai/path", "api.esphub.ai:443", "user@hub", "hub?x", "hub#x",
        " hub", "hub ", "a..b", ".hub", "hub.", "-hub", "hub-", "hub-.lab", "host_name",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local"};
    char error[128], record[256];
    assert(!eh_hub_host_valid(NULL));
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        assert(!eh_hub_host_valid(invalid[i]));
        snprintf(record, sizeof(record), "{\"hub\":\"%s\",\"token\":\"eh_dev_fixture\"}", invalid[i]);
        assert(!prov_apply(record, error, sizeof(error)));
        assert(writes == 0);
    }
    assert(eh_hub_host_valid("localhost"));
    assert(eh_hub_host_valid("LAB-2.example.org"));
    char longest[EH_HUB_HOST_MAX + 2];
    memset(longest, 'a', sizeof(longest));
    longest[63] = '.';
    longest[sizeof(longest) - 1] = 0;
    assert(!eh_hub_host_valid(longest));
    longest[EH_HUB_HOST_MAX] = 0;
    assert(eh_hub_host_valid(longest));

    assert(prov_apply("{\"token\":\"eh_dev_fixture\"}",error,sizeof(error)));
    assert(!strcmp(stored_hub,"api.esphub.ai"));
    assert(prov_apply("{\"hub\":\"lab-2.example.org\"}",error,sizeof(error)));
    assert(!strcmp(stored_hub,"lab-2.example.org"));
    return 0;
}
