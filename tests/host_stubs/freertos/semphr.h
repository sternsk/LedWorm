#pragma once
#include <mutex>
using SemaphoreHandle_t = std::mutex *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned) {
    mutex->lock();
    return 1;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
