/**
 */
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define millis() (esp_timer_get_time() / 1000)