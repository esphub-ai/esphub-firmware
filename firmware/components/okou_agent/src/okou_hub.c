/* SPDX-License-Identifier: MIT */
#include "okou_hub.h"
#include <string.h>

bool okou_hub_host_valid(const char *host)
{
    size_t length = host ? strlen(host) : 0;
    if (!length || length > OKOU_HUB_HOST_MAX) return false;
    size_t label = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (c == '.') {
            if (!label || host[i - 1] == '-') return false;
            label = 0;
        } else {
            bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!alnum && (c != '-' || !label)) return false;
            if (++label > 63) return false;
        }
    }
    return label && host[length - 1] != '-';
}
