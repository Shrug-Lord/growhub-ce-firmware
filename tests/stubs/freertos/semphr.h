#pragma once
#include <pthread.h>
#include <stdlib.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t m = malloc(sizeof(*m));
    if (m) pthread_mutex_init(m, NULL);
    return m;
}
static inline void xSemaphoreTake(SemaphoreHandle_t m, int wait) {
    (void)wait; pthread_mutex_lock(m);
}
static inline void xSemaphoreGive(SemaphoreHandle_t m) { pthread_mutex_unlock(m); }
