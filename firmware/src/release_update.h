#pragma once
#include <stdbool.h>
#include <stddef.h>
void release_update_init(void);
// Caller owns returned JSON. Snapshot does not perform network I/O.
char *release_update_json(void);
// Validated, bounded, non-retained action. Work is queued off caller's task.
bool release_update_action(const char *json, size_t length);
