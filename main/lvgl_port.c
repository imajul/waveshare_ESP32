#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board.h"

static const char *TAG = "lvgl";

#define DRAW_BUF_LINES   20
#define TICK_PERIOD_MS   2

static SemaphoreHandle_t s_lock;
static uint8_t s_touch_points;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *map)
{
    // En el panel RGB, draw_bitmap copia al framebuffer (PSRAM) de forma síncrona.
    esp_lcd_panel_draw_bitmap(board_lcd_panel(), a->x1, a->y1, a->x2 + 1, a->y2 + 1, map);
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = board_touch();
    uint16_t x[5], y[5];
    uint8_t cnt = 0;
    if (tp) {
        esp_lcd_touch_read_data(tp);
        esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 5);
    }
    s_touch_points = cnt;
    if (cnt > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg)
{
    lv_tick_inc(TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    while (1) {
        uint32_t wait = 10;
        if (lvgl_lock(-1)) {
            wait = lv_timer_handler();
            lvgl_unlock();
        }
        if (wait < 2) wait = 2;
        if (wait > 30) wait = 30;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

bool lvgl_lock(int timeout_ms)
{
    TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lock, t) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

uint8_t lvgl_touch_points(void)
{
    return s_touch_points;
}

esp_err_t lvgl_port_start(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    lv_init();

    size_t px = BOARD_LCD_H_RES * DRAW_BUF_LINES;
    lv_color_t *b1 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t *b2 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "Sin memoria para los buffers de dibujo");
        return ESP_ERR_NO_MEM;
    }
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, b1, b2, px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = BOARD_LCD_H_RES;
    disp_drv.ver_res = BOARD_LCD_V_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t targs = { .callback = tick_cb, .name = "lv_tick" };
    esp_timer_handle_t timer;
    esp_timer_create(&targs, &timer);
    esp_timer_start_periodic(timer, TICK_PERIOD_MS * 1000);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);
    return ESP_OK;
}
