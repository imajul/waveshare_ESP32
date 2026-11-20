idf_component_register(
    SRCS "main.c" "board.c" "lvgl_port.c" "rtc_pcf85063.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_lcd_touch esp_lcd_touch_gt911 lvgl esp_driver_i2c esp_driver_gpio
             esp_driver_uart driver esp_driver_sdspi esp_driver_spi fatfs sdmmc
             esp_wifi esp_netif esp_event nvs_flash esp_timer esp_psram spi_flash esp_system esp_app_format
    EMBED_FILES "Montserrat-Medium.ttf"
)
