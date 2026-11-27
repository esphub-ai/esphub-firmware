#include <assert.h>
#include <string.h>
#include "eh_hub.h"

int main(void)
{
    const char *invalid[] = {"", "wss://api.esphub.ai", "https://api.esphub.ai", "http://hub/",
        "api.esphub.ai/", "api.esphub.ai/path", "api.esphub.ai:443", "user@hub", "hub?x", "hub#x",
        " hub", "hub ", "a..b", ".hub", "hub.", "-hub", "hub-", "hub-.lab", "host_name",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local"};
    assert(!eh_hub_host_valid(NULL));
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i)
        assert(!eh_hub_host_valid(invalid[i]));
    assert(eh_hub_host_valid("localhost"));
    assert(eh_hub_host_valid("LAB-2.example.org"));
    char longest[EH_HUB_HOST_MAX + 2];
    memset(longest, 'a', sizeof(longest));
    longest[63] = '.';
    longest[sizeof(longest) - 1] = 0;
    assert(!eh_hub_host_valid(longest));
    longest[EH_HUB_HOST_MAX] = 0;
    assert(eh_hub_host_valid(longest));
    return 0;
}
