#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Match a complete form key, then decode its value. A suffix such as
// outlet_mode must never also be interpreted as the bulk mode command.
static inline bool get_param(const char *query, const char *key, char *val, size_t val_len)
{
    if (!query || !key || !val || val_len == 0) return false;
    char search[64];
    int n = snprintf(search, sizeof(search), "%s=", key);
    if (n < 0 || (size_t)n >= sizeof(search)) return false;
    const char *p = query;
    while ((p = strstr(p, search))) {
        if (p == query || p[-1] == '&') break;
        p++;
    }
    if (!p) return false;
    p += (size_t)n;
    size_t i = 0;
    while (*p && *p != '&' && i < val_len - 1) {
        if (*p == '+') {
            val[i++] = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            val[i++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            val[i++] = *p;
        }
        p++;
    }
    val[i] = '\0';
    return true;
}
