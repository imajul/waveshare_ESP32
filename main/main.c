// Firmware de prueba para Waveshare ESP32-S3-Touch-LCD-4.3B
#include "esp_log.h"
#include "nvs_flash.h"
#include "board.h"
#include "lvgl_port.h"

void ui_create(void);

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(lvgl_port_start());

    lvgl_lock(-1);
    ui_create();
    lvgl_unlock();

    ESP_LOGI(TAG, "Firmware de prueba en marcha");
}
