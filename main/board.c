#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

// ---------------------------------------------------------------------------
// CH422G: no tiene dirección I2C propia; cada "registro" se direcciona como si
// fuera un esclavo distinto.
// ---------------------------------------------------------------------------
#define CH422G_ADDR_MODE    0x24
#define CH422G_ADDR_OC_OUT  0x23
#define CH422G_ADDR_IO_OUT  0x38
#define CH422G_ADDR_IO_IN   0x26
#define CH422G_MODE_IO_OE   0x01   // IO0..IO7 como salidas push-pull

static i2c_master_dev_handle_t s_ch_mode, s_ch_oc, s_ch_io_out, s_ch_io_in;
static SemaphoreHandle_t s_ch_lock;
// Estado inicial: DI0/DI1 en alto (no pelean con el optoacoplador),
// resets en alto, retroiluminación encendida, SD_CS en bajo (único dispositivo SPI).
static uint8_t s_io_state = EXIO_DI0 | EXIO_TP_RST | EXIO_BACKLIGHT | EXIO_LCD_RST | EXIO_DI1;
static uint8_t s_oc_state = 0;

static esp_err_t ch_write(i2c_master_dev_handle_t dev, uint8_t v)
{
    return i2c_master_transmit(dev, &v, 1, 100);
}

static esp_err_t ch422g_apply_io(void)
{
    ESP_RETURN_ON_ERROR(ch_write(s_ch_mode, CH422G_MODE_IO_OE), TAG, "ch422g mode");
    return ch_write(s_ch_io_out, s_io_state);
}

static esp_err_t ch422g_set_bits(uint8_t mask, bool level)
{
    xSemaphoreTake(s_ch_lock, portMAX_DELAY);
    if (level) {
        s_io_state |= mask;
    } else {
        s_io_state &= ~mask;
    }
    esp_err_t err = ch_write(s_ch_io_out, s_io_state);
    xSemaphoreGive(s_ch_lock);
    return err;
}

static esp_err_t ch422g_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = 400000,
    };
    cfg.device_address = CH422G_ADDR_MODE;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_ch_mode), TAG, "");
    cfg.device_address = CH422G_ADDR_OC_OUT;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_ch_oc), TAG, "");
    cfg.device_address = CH422G_ADDR_IO_OUT;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_ch_io_out), TAG, "");
    cfg.device_address = CH422G_ADDR_IO_IN;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_ch_io_in), TAG, "");
    s_ch_lock = xSemaphoreCreateMutex();

    ESP_RETURN_ON_ERROR(ch422g_apply_io(), TAG, "CH422G no responde");
    ESP_RETURN_ON_ERROR(ch_write(s_ch_oc, s_oc_state), TAG, "");
    return ESP_OK;
}

void board_backlight_set(bool on)
{
    ch422g_set_bits(EXIO_BACKLIGHT, on);
}

bool board_backlight_get(void)
{
    return s_io_state & EXIO_BACKLIGHT;
}

esp_err_t board_do_write(uint8_t mask)
{
    xSemaphoreTake(s_ch_lock, portMAX_DELAY);
    s_oc_state = mask & (OC_DO0 | OC_DO1);
    esp_err_t err = ch_write(s_ch_oc, s_oc_state);
    xSemaphoreGive(s_ch_lock);
    return err;
}

uint8_t board_do_get(void)
{
    return s_oc_state;
}

esp_err_t board_di_read(uint8_t *mask)
{
    // El CH422G solo permite todos los IO como entrada o todos como salida.
    // Igual que el ejemplo oficial: pasamos a entrada, leemos y restauramos.
    uint8_t v = 0;
    xSemaphoreTake(s_ch_lock, portMAX_DELAY);
    esp_err_t err = ch_write(s_ch_mode, 0x00);
    if (err == ESP_OK) {
        esp_rom_delay_us(200);
        err = i2c_master_receive(s_ch_io_in, &v, 1, 100);
    }
    esp_err_t err2 = ch422g_apply_io();
    xSemaphoreGive(s_ch_lock);
    *mask = v & (EXIO_DI0 | EXIO_DI1);
    return err != ESP_OK ? err : err2;
}

// ---------------------------------------------------------------------------
// LCD RGB (ST7262, 800x480, RGB565 16 bits)
// ---------------------------------------------------------------------------
static esp_err_t lcd_init(void)
{
    // Reset del LCD por el expansor
    ch422g_set_bits(EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    ch422g_set_bits(EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = 46,
        .vsync_gpio_num = 3,
        .de_gpio_num = 5,
        .pclk_gpio_num = 7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            14, 38, 18, 17, 10,         // B3..B7
            39, 0, 45, 48, 47, 21,      // G2..G7
            1, 2, 42, 41, 40,           // R3..R7
        },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "panel RGB");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Táctil GT911
// ---------------------------------------------------------------------------
static esp_err_t touch_init(void)
{
    // Secuencia de reset: INT en bajo durante el reset -> dirección 0x5D
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ch422g_set_bits(EXIO_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    ch422g_set_bits(EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_reset_pin(BOARD_TOUCH_INT);
    gpio_set_direction(BOARD_TOUCH_INT, GPIO_MODE_INPUT);

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &tp_io), TAG, "touch io");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
    };
    return esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
}

esp_err_t board_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "I2C");
    ESP_RETURN_ON_ERROR(ch422g_init(), TAG, "CH422G");
    ESP_RETURN_ON_ERROR(lcd_init(), TAG, "LCD");
    esp_err_t err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 no inicializado (%s); se continúa sin táctil", esp_err_to_name(err));
        s_touch = NULL;
    }
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
esp_lcd_panel_handle_t board_lcd_panel(void) { return s_panel; }
esp_lcd_touch_handle_t board_touch(void) { return s_touch; }
