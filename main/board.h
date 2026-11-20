// Soporte de placa: Waveshare ESP32-S3-Touch-LCD-4.3B
// Pines tomados de los ejemplos oficiales (github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3B)
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#define BOARD_LCD_H_RES         800
#define BOARD_LCD_V_RES         480

// I2C compartido: CH422G, GT911, PCF85063A
#define BOARD_I2C_SDA           8
#define BOARD_I2C_SCL           9
#define BOARD_TOUCH_INT         4

// RS485 (UART)
#define BOARD_RS485_TX          44
#define BOARD_RS485_RX          43

// CAN (TWAI)
#define BOARD_CAN_TX            15
#define BOARD_CAN_RX            16

// microSD (SPI). El CS está en el expansor CH422G (EXIO4).
#define BOARD_SD_MOSI           11
#define BOARD_SD_CLK            12
#define BOARD_SD_MISO           13

#define BOARD_RTC_ADDR          0x51

// Bits del puerto IO del CH422G
#define EXIO_DI0                (1 << 0)
#define EXIO_TP_RST             (1 << 1)
#define EXIO_BACKLIGHT          (1 << 2)
#define EXIO_LCD_RST            (1 << 3)
#define EXIO_SD_CS              (1 << 4)
#define EXIO_DI1                (1 << 5)

// Bits del puerto OC del CH422G (salidas digitales aisladas)
#define OC_DO0                  (1 << 0)
#define OC_DO1                  (1 << 1)

esp_err_t board_init(void);

i2c_master_bus_handle_t board_i2c_bus(void);
esp_lcd_panel_handle_t board_lcd_panel(void);
esp_lcd_touch_handle_t board_touch(void);

void board_backlight_set(bool on);
bool board_backlight_get(void);

// Salidas DO0/DO1 (máscara OC_DO0 | OC_DO1)
esp_err_t board_do_write(uint8_t mask);
uint8_t board_do_get(void);
// Lee DI0/DI1. Devuelve bits EXIO_DI0 / EXIO_DI1.
esp_err_t board_di_read(uint8_t *mask);
