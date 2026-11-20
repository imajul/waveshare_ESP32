#include "rtc_pcf85063.h"
#include "board.h"

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

esp_err_t ext_rtc_init(void)
{
    if (s_dev) return ESP_OK;
    esp_err_t err = i2c_master_probe(board_i2c_bus(), BOARD_RTC_ADDR, 100);
    if (err != ESP_OK) return err;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_RTC_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_dev);
}

esp_err_t ext_rtc_get(struct tm *t, bool *lost)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t reg = 0x04, d[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, d, 7, 100);
    if (err != ESP_OK) return err;
    *lost = d[0] & 0x80;
    t->tm_sec = bcd2bin(d[0] & 0x7F);
    t->tm_min = bcd2bin(d[1] & 0x7F);
    t->tm_hour = bcd2bin(d[2] & 0x3F);
    t->tm_mday = bcd2bin(d[3] & 0x3F);
    t->tm_wday = d[4] & 0x07;
    t->tm_mon = bcd2bin(d[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(d[6]) + 100;
    return ESP_OK;
}

esp_err_t ext_rtc_set(const struct tm *t)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t d[8] = {
        0x04,
        bin2bcd(t->tm_sec),       // escribir segundos borra el flag OS
        bin2bcd(t->tm_min),
        bin2bcd(t->tm_hour),
        bin2bcd(t->tm_mday),
        (uint8_t)t->tm_wday,
        bin2bcd(t->tm_mon + 1),
        bin2bcd(t->tm_year % 100),
    };
    return i2c_master_transmit(s_dev, d, sizeof(d), 100);
}
