#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t ext_rtc_init(void);
// lost = true si el oscilador se paró (hora no válida, p. ej. sin batería)
esp_err_t ext_rtc_get(struct tm *t, bool *lost);
esp_err_t ext_rtc_set(const struct tm *t);
