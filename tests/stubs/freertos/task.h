#pragma once
static inline void vTaskDelay(int ms) { (void)ms; }
static inline void xTaskCreate(void (*fn)(void *), const char *name, int stack,
                              void *arg, int priority, void *handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
}
