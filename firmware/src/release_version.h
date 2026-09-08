#pragma once
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
static inline bool release_version_parse(const char *s, unsigned out[3]) {
    if (*s == 'v') s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s) || (*s == '0' && isdigit((unsigned char)s[1]))) return false;
        unsigned v = 0;
        while (isdigit((unsigned char)*s)) { if (v > 100000) return false; v = v * 10 + *s++ - '0'; }
        out[i] = v;
        if (i < 2 && *s++ != '.') return false;
    }
    return strcmp(s, "C") == 0;
}
static inline bool release_version_newer(const char *s) {
    unsigned a[3], b[3];
    if (!release_version_parse(s, a) || !release_version_parse(GROWHUB_VERSION, b)) return false;
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return false;
}
