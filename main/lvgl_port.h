#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

esp_err_t lvgl_port_start(void);
// Protege las llamadas a LVGL hechas desde tareas distintas a la de LVGL.
bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);
// Número de dedos detectados en la última lectura del táctil.
uint8_t lvgl_touch_points(void);
