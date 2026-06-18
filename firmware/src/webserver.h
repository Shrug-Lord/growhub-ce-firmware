#pragma once

#include <stdbool.h>

// Start the HTTP server on port 80.
// Serves: config page, status JSON, OTA upload endpoint.
void webserver_init(void);

// True once the HTTP server and expected handlers are registered.
bool webserver_is_running(void);
