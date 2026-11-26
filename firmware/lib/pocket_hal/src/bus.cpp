#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "board.h"

static SemaphoreHandle_t gBus = nullptr;

void busLockInit() {
  if (!gBus) gBus = xSemaphoreCreateRecursiveMutex();
}
void busLock() {
  if (!gBus) busLockInit();
  xSemaphoreTakeRecursive(gBus, portMAX_DELAY);
}
void busUnlock() {
  if (gBus) xSemaphoreGiveRecursive(gBus);
}
