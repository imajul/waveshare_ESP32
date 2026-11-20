// Interfaz de prueba de la placa Waveshare ESP32-S3-Touch-LCD-4.3B
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "board.h"
#include "lvgl_port.h"
#include "rtc_pcf85063.h"

static const char *TAG = "ui";

extern const uint8_t ttf_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t ttf_end[] asm("_binary_Montserrat_Medium_ttf_end");

static lv_font_t *f16, *f20, *f28, *f64;

#define COL_OK   lv_palette_main(LV_PALETTE_GREEN)
#define COL_ERR  lv_palette_main(LV_PALETTE_RED)
#define COL_WARN lv_palette_main(LV_PALETTE_AMBER)

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static lv_obj_t *mk_row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(r, 12, 0);
    return r;
}

static lv_obj_t *mk_log(lv_obj_t *parent)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_flex_grow(ta, 1);
    lv_textarea_set_text(ta, "");
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_obj_set_style_text_font(ta, f16, 0);
    return ta;
}

// Añade una línea a un textarea. Llamar con el lock de LVGL tomado.
static void log_add(lv_obj_t *ta, const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    if (strlen(lv_textarea_get_text(ta)) > 3000) {
        lv_textarea_set_text(ta, "");
    }
    lv_textarea_add_text(ta, buf);
}

static void set_status(lv_obj_t *lbl, lv_color_t c, const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, c, 0);
}

static lv_obj_t *setup_tab(lv_obj_t *tab, const char *title)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);
    lv_obj_t *t = mk_label(tab, f28, title);
    lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_BLUE), 0);
    return t;
}

// ---------------------------------------------------------------------------
// Pestaña: Sistema
// ---------------------------------------------------------------------------
static lv_obj_t *sys_info, *sys_live, *sys_i2c;

static void sys_timer_cb(lv_timer_t *t)
{
    int64_t s = esp_timer_get_time() / 1000000;
    lv_label_set_text_fmt(sys_live,
        "Tiempo encendido: %02d:%02d:%02d\n"
        "RAM interna libre: %u KB (mínimo %u KB)\n"
        "PSRAM libre: %u KB",
        (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static const char *i2c_name(uint8_t a)
{
    switch (a) {
    case 0x5D: case 0x14: return "GT911 táctil";
    case 0x51: return "PCF85063A RTC";
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        return "CH422G expansor";
    default: return "?";
    }
}

static void sys_scan_cb(lv_event_t *e)
{
    char buf[600] = "Dispositivos I2C:\n";
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(board_i2c_bus(), a, 20) == ESP_OK) {
            char line[60];
            snprintf(line, sizeof(line), "  0x%02X  %s\n", a, i2c_name(a));
            if (strlen(buf) + strlen(line) < sizeof(buf)) strcat(buf, line);
            found++;
        }
    }
    if (!found) strcat(buf, "  (ninguno)\n");
    lv_label_set_text(sys_i2c, buf);
}

static void build_tab_system(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_SETTINGS "  Sistema");

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const esp_app_desc_t *app = esp_app_get_description();

    sys_info = mk_label(tab, f20, "");
    lv_label_set_text_fmt(sys_info,
        "Chip: ESP32-S3 rev %d.%d, %d núcleos @ %d MHz\n"
        "Flash: %u MB    PSRAM: %u MB\n"
        "MAC WiFi: %02X:%02X:%02X:%02X:%02X:%02X\n"
        "ESP-IDF %s  -  compilado %s %s",
        ci.revision / 100, ci.revision % 100, ci.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        (unsigned)(flash / (1024 * 1024)), (unsigned)(esp_psram_get_size() / (1024 * 1024)),
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        app->idf_ver, app->date, app->time);

    sys_live = mk_label(tab, f20, "");
    lv_timer_create(sys_timer_cb, 1000, NULL);
    sys_timer_cb(NULL);

    mk_btn(tab, LV_SYMBOL_REFRESH "  Escanear bus I2C", sys_scan_cb, NULL);
    sys_i2c = mk_label(tab, f16, "");
    sys_scan_cb(NULL);
}

// ---------------------------------------------------------------------------
// Pestaña: Táctil
// ---------------------------------------------------------------------------
#define CANVAS_W 600
#define CANVAS_H 360
static lv_obj_t *touch_canvas, *touch_lbl;
static lv_color_t touch_color;

static void touch_draw_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(touch_canvas, &a);
    int x = p.x - a.x1, y = p.y - a.y1;
    lv_label_set_text_fmt(touch_lbl, "X: %d   Y: %d   Dedos: %d", p.x, p.y, lvgl_touch_points());
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = touch_color;
    d.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(touch_canvas, x - 5, y - 5, 10, 10, &d);
}

static void touch_clear_cb(lv_event_t *e)
{
    lv_canvas_fill_bg(touch_canvas, lv_color_white(), LV_OPA_COVER);
}

static void touch_color_cb(lv_event_t *e)
{
    touch_color = lv_palette_main((lv_palette_t)(intptr_t)lv_event_get_user_data(e));
}

static void build_tab_touch(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 10, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = mk_row(tab);
    touch_lbl = mk_label(row, f20, "Dibuja en el recuadro");
    lv_obj_set_width(touch_lbl, 330);
    mk_btn(row, LV_SYMBOL_TRASH " Borrar", touch_clear_cb, NULL);
    static const lv_palette_t pals[] = { LV_PALETTE_RED, LV_PALETTE_GREEN, LV_PALETTE_BLUE, LV_PALETTE_ORANGE };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = mk_btn(row, "", touch_color_cb, (void *)(intptr_t)pals[i]);
        lv_obj_set_size(b, 40, 40);
        lv_obj_set_style_bg_color(b, lv_palette_main(pals[i]), 0);
    }
    touch_color = lv_palette_main(LV_PALETTE_RED);

    lv_color_t *buf = heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H), MALLOC_CAP_SPIRAM);
    touch_canvas = lv_canvas_create(tab);
    lv_canvas_set_buffer(touch_canvas, buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_style_border_width(touch_canvas, 2, 0);
    lv_obj_set_style_border_color(touch_canvas, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_add_flag(touch_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch_canvas, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(touch_canvas, touch_draw_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(touch_canvas, touch_draw_cb, LV_EVENT_PRESSED, NULL);
    touch_clear_cb(NULL);
}

// ---------------------------------------------------------------------------
// Pestaña: Pantalla (colores + retroiluminación)
// ---------------------------------------------------------------------------
static lv_timer_t *bl_timer;

static void overlay_close_cb(lv_event_t *e)
{
    lv_obj_del(lv_event_get_current_target(e));
}

static lv_obj_t *overlay_create(lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, overlay_close_cb, LV_EVENT_CLICKED, NULL);
    return o;
}

static void color_cb(lv_event_t *e)
{
    uint32_t c = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    overlay_create(lv_color_hex(c));
}

static void bars_cb(lv_event_t *e)
{
    lv_obj_t *o = overlay_create(lv_color_black());
    static const uint32_t cols[] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    int w = BOARD_LCD_H_RES / 8;
    for (int i = 0; i < 8; i++) {
        lv_obj_t *b = lv_obj_create(o);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, i * w, 0);
        lv_obj_set_size(b, w, 320);
        lv_obj_set_style_bg_color(b, lv_color_hex(cols[i]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
    static const uint32_t grads[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *g = lv_obj_create(o);
        lv_obj_remove_style_all(g);
        lv_obj_set_pos(g, 0, 320 + i * 40);
        lv_obj_set_size(g, BOARD_LCD_H_RES, 40);
        lv_obj_set_style_bg_color(g, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(g, lv_color_hex(grads[i]), 0);
        lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void bl_restore_cb(lv_timer_t *t)
{
    board_backlight_set(true);
    bl_timer = NULL;
}

static void bl_test_cb(lv_event_t *e)
{
    if (bl_timer) return;
    board_backlight_set(false);
    bl_timer = lv_timer_create(bl_restore_cb, 3000, NULL);
    lv_timer_set_repeat_count(bl_timer, 1);
}

static void build_tab_display(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_IMAGE "  Pantalla");
    mk_label(tab, f20, "Busca píxeles muertos o colores raros.\nToca la pantalla para volver.");
    lv_obj_t *row = mk_row(tab);
    struct { const char *n; uint32_t c; } cs[] = {
        { "Rojo", 0xFF0000 }, { "Verde", 0x00FF00 }, { "Azul", 0x0000FF },
        { "Blanco", 0xFFFFFF }, { "Negro", 0x000000 },
    };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = mk_btn(row, cs[i].n, color_cb, (void *)(uintptr_t)cs[i].c);
        lv_obj_set_size(b, 110, 60);
    }
    lv_obj_t *b = mk_btn(tab, LV_SYMBOL_LIST "  Barras y degradados", bars_cb, NULL);
    lv_obj_set_height(b, 60);

    mk_label(tab, f20, "Retroiluminación (EXIO2 del CH422G):");
    b = mk_btn(tab, LV_SYMBOL_EYE_CLOSE "  Apagar 3 segundos", bl_test_cb, NULL);
    lv_obj_set_height(b, 60);
}

// ---------------------------------------------------------------------------
// Pestaña: WiFi
// ---------------------------------------------------------------------------
static lv_obj_t *wifi_list, *wifi_status, *wifi_btn;
static bool wifi_ready, wifi_busy;

static void wifi_init_once(void)
{
    if (wifi_ready) return;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_ready = true;
}

static const char *auth_name(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN: return "abierta";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "otra";
    }
}

static void wifi_scan_task(void *arg)
{
    wifi_init_once();
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    uint16_t n = 30;
    wifi_ap_record_t *aps = calloc(n, sizeof(wifi_ap_record_t));
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&n, aps);

    lvgl_lock(-1);
    lv_obj_clean(wifi_list);
    if (err != ESP_OK) {
        set_status(wifi_status, COL_ERR, "Error al escanear: %s", esp_err_to_name(err));
    } else {
        set_status(wifi_status, n ? COL_OK : COL_WARN, "%d redes encontradas", n);
        for (int i = 0; i < n; i++) {
            char line[100];
            snprintf(line, sizeof(line), "%s   (%d dBm, canal %d, %s)",
                     aps[i].ssid[0] ? (char *)aps[i].ssid : "<oculta>",
                     aps[i].rssi, aps[i].primary, auth_name(aps[i].authmode));
            lv_obj_t *b = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, line);
            lv_obj_set_style_text_font(b, f16, 0);
        }
    }
    lv_obj_clear_state(wifi_btn, LV_STATE_DISABLED);
    lvgl_unlock();
    free(aps);
    wifi_busy = false;
    vTaskDelete(NULL);
}

static void wifi_scan_cb(lv_event_t *e)
{
    if (wifi_busy) return;
    wifi_busy = true;
    lv_obj_add_state(wifi_btn, LV_STATE_DISABLED);
    set_status(wifi_status, COL_WARN, "Escaneando...");
    xTaskCreate(wifi_scan_task, "wifi_scan", 6144, NULL, 3, NULL);
}

static void build_tab_wifi(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_WIFI "  WiFi");
    lv_obj_t *row = mk_row(tab);
    wifi_btn = mk_btn(row, LV_SYMBOL_REFRESH "  Buscar redes", wifi_scan_cb, NULL);
    wifi_status = mk_label(row, f20, "Pulsa para buscar redes WiFi (2,4 GHz)");
    wifi_list = lv_list_create(tab);
    lv_obj_set_width(wifi_list, LV_PCT(100));
    lv_obj_set_flex_grow(wifi_list, 1);
}

// ---------------------------------------------------------------------------
// Pestaña: Reloj (RTC PCF85063A)
// ---------------------------------------------------------------------------
static lv_obj_t *rtc_time, *rtc_date, *rtc_status;
static bool rtc_ok;

static void rtc_timer_cb(lv_timer_t *t)
{
    static const char *dias[] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
    static const char *meses[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                                   "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    if (!rtc_ok) return;
    struct tm tm;
    bool lost;
    if (ext_rtc_get(&tm, &lost) != ESP_OK) {
        set_status(rtc_status, COL_ERR, "Error leyendo el RTC");
        return;
    }
    lv_label_set_text_fmt(rtc_time, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (tm.tm_mon >= 0 && tm.tm_mon < 12 && tm.tm_wday < 7) {
        lv_label_set_text_fmt(rtc_date, "%s, %d de %s de %d", dias[tm.tm_wday], tm.tm_mday,
                              meses[tm.tm_mon], tm.tm_year + 1900);
    }
    if (lost) {
        set_status(rtc_status, COL_WARN,
                   "El RTC perdió la hora (¿sin pila?). Pulsa el botón para ajustarlo.");
    } else {
        set_status(rtc_status, COL_OK, "RTC funcionando. Desconecta la placa y comprueba "
                                       "que la hora sigue al volver (necesita pila).");
    }
}

static void rtc_set_cb(lv_event_t *e)
{
    // Ajusta a la fecha y hora de compilación del firmware
    static const char *m = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0 };
    struct tm tm = { 0 };
    sscanf(__DATE__, "%3s %d %d", mon, &tm.tm_mday, &tm.tm_year);
    sscanf(__TIME__, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_mon = (strstr(m, mon) - m) / 3;
    tm.tm_year -= 1900;
    mktime(&tm); // calcula tm_wday
    if (ext_rtc_set(&tm) == ESP_OK) {
        rtc_timer_cb(NULL);
    } else {
        set_status(rtc_status, COL_ERR, "Error escribiendo el RTC");
    }
}

static void build_tab_rtc(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_BELL "  Reloj (RTC)");
    rtc_time = mk_label(tab, f64, "--:--:--");
    rtc_date = mk_label(tab, f28, "");
    rtc_status = mk_label(tab, f20, "");
    lv_obj_set_width(rtc_status, LV_PCT(100));
    lv_label_set_long_mode(rtc_status, LV_LABEL_LONG_WRAP);
    mk_btn(tab, LV_SYMBOL_EDIT "  Ajustar a la hora de compilación", rtc_set_cb, NULL);

    rtc_ok = ext_rtc_init() == ESP_OK;
    if (!rtc_ok) {
        set_status(rtc_status, COL_ERR, "No se encontró el PCF85063A en 0x51");
    }
    lv_timer_create(rtc_timer_cb, 500, NULL);
    rtc_timer_cb(NULL);
}

// ---------------------------------------------------------------------------
// Pestaña: microSD
// ---------------------------------------------------------------------------
static lv_obj_t *sd_log, *sd_btn;
static sdmmc_card_t *sd_card;
static bool sd_bus_ready, sd_busy;

#define SD_LOG(...) do { lvgl_lock(-1); log_add(sd_log, __VA_ARGS__); lvgl_unlock(); } while (0)

static void sd_task(void *arg)
{
    const char *mp = "/sdcard";
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    esp_err_t err;

    if (sd_card) {
        esp_vfs_fat_sdcard_unmount(mp, sd_card);
        sd_card = NULL;
        SD_LOG("Tarjeta desmontada.");
    }
    if (!sd_bus_ready) {
        spi_bus_config_t bus = {
            .mosi_io_num = BOARD_SD_MOSI,
            .miso_io_num = BOARD_SD_MISO,
            .sclk_io_num = BOARD_SD_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 16 * 1024,
        };
        err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            SD_LOG("Error iniciando SPI: %s", esp_err_to_name(err));
            goto done;
        }
        sd_bus_ready = true;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_NC;  // CS fijo en bajo por el CH422G (EXIO4)
    slot.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    SD_LOG("Montando la tarjeta...");
    err = esp_vfs_fat_sdspi_mount(mp, &host, &slot, &mcfg, &sd_card);
    if (err != ESP_OK) {
        SD_LOG("No se pudo montar: %s", esp_err_to_name(err));
        SD_LOG("¿Hay tarjeta? Debe estar en FAT32.");
        sd_card = NULL;
        goto done;
    }
    uint64_t mb = (uint64_t)sd_card->csd.capacity * sd_card->csd.sector_size / (1024 * 1024);
    SD_LOG("Tarjeta: %s, %llu MB, %s", sd_card->cid.name, mb,
           (sd_card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC");

    // Prueba de escritura y lectura
    const char *path = "/sdcard/prueba.bin";
    const size_t chunk = 16 * 1024, total = 1024 * 1024;
    uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_DMA);
    for (size_t i = 0; i < chunk; i++) buf[i] = (uint8_t)(i * 7);

    FILE *f = fopen(path, "wb");
    if (!f) {
        SD_LOG("No se pudo crear el archivo");
        free(buf);
        goto done;
    }
    int64_t t0 = esp_timer_get_time();
    for (size_t w = 0; w < total; w += chunk) fwrite(buf, 1, chunk, f);
    fclose(f);
    int64_t t1 = esp_timer_get_time();
    SD_LOG("Escritura 1 MB: %.0f KB/s", 1024.0 * 1e6 / (t1 - t0));

    bool ok = true;
    f = fopen(path, "rb");
    t0 = esp_timer_get_time();
    for (size_t r = 0; r < total && ok; r += chunk) {
        memset(buf, 0, chunk);
        if (fread(buf, 1, chunk, f) != chunk) ok = false;
        for (size_t i = 0; i < chunk && ok; i++) if (buf[i] != (uint8_t)(i * 7)) ok = false;
    }
    fclose(f);
    t1 = esp_timer_get_time();
    unlink(path);
    free(buf);
    SD_LOG("Lectura 1 MB: %.0f KB/s", 1024.0 * 1e6 / (t1 - t0));
    SD_LOG(ok ? "Verificación de datos: OK" : "Verificación de datos: ¡ERROR!");

done:
    lvgl_lock(-1);
    lv_obj_clear_state(sd_btn, LV_STATE_DISABLED);
    lvgl_unlock();
    sd_busy = false;
    vTaskDelete(NULL);
}

static void sd_test_cb(lv_event_t *e)
{
    if (sd_busy) return;
    sd_busy = true;
    lv_obj_add_state(sd_btn, LV_STATE_DISABLED);
    xTaskCreate(sd_task, "sd_test", 6144, NULL, 3, NULL);
}

static void build_tab_sd(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_SD_CARD "  microSD");
    mk_label(tab, f16, "Inserta una tarjeta en FAT32. Se escribe y lee un archivo de 1 MB "
                       "y se borra al terminar.");
    sd_btn = mk_btn(tab, LV_SYMBOL_PLAY "  Montar y probar", sd_test_cb, NULL);
    sd_log = mk_log(tab);
}

// ---------------------------------------------------------------------------
// Pestaña: E/S digitales aisladas
// ---------------------------------------------------------------------------
static lv_obj_t *io_led[2], *io_sw[2], *io_auto, *io_status;

static void io_read(void)
{
    uint8_t di;
    if (board_di_read(&di) != ESP_OK) {
        set_status(io_status, COL_ERR, "Error leyendo el CH422G");
        return;
    }
    bool v[2] = { di & EXIO_DI0, di & EXIO_DI1 };
    for (int i = 0; i < 2; i++) {
        if (v[i]) lv_led_on(io_led[i]); else lv_led_off(io_led[i]);
    }
    set_status(io_status, lv_color_black(), "DI0 = %d    DI1 = %d", v[0], v[1]);
}

static void io_read_cb(lv_event_t *e) { io_read(); }

static void io_timer_cb(lv_timer_t *t)
{
    if (lv_obj_has_state(io_auto, LV_STATE_CHECKED)) io_read();
}

static void io_sw_cb(lv_event_t *e)
{
    uint8_t m = 0;
    if (lv_obj_has_state(io_sw[0], LV_STATE_CHECKED)) m |= OC_DO0;
    if (lv_obj_has_state(io_sw[1], LV_STATE_CHECKED)) m |= OC_DO1;
    board_do_write(m);
}

static void build_tab_io(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_SHUFFLE "  Entradas / Salidas");
    lv_obj_t *l = mk_label(tab, f16,
        "Salidas DO0/DO1 (OC0/OC1 del CH422G) y entradas DI0/DI1 (IO0/IO5), aisladas. "
        "Prueba rápida: conecta DO0 con DI0 y DO1 con DI1 según el esquema de Waveshare "
        "y activa las salidas.");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);

    lv_obj_t *row = mk_row(tab);
    for (int i = 0; i < 2; i++) {
        mk_label(row, f20, i == 0 ? "DO0" : "DO1");
        io_sw[i] = lv_switch_create(row);
        lv_obj_set_size(io_sw[i], 80, 40);
        lv_obj_add_event_cb(io_sw[i], io_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_t *sp = lv_obj_create(row);
        lv_obj_remove_style_all(sp);
        lv_obj_set_size(sp, 30, 1);
    }

    row = mk_row(tab);
    for (int i = 0; i < 2; i++) {
        mk_label(row, f20, i == 0 ? "DI0" : "DI1");
        io_led[i] = lv_led_create(row);
        lv_obj_set_size(io_led[i], 36, 36);
        lv_led_set_color(io_led[i], lv_palette_main(LV_PALETTE_GREEN));
        lv_led_off(io_led[i]);
        lv_obj_t *sp = lv_obj_create(row);
        lv_obj_remove_style_all(sp);
        lv_obj_set_size(sp, 30, 1);
    }

    row = mk_row(tab);
    mk_btn(row, LV_SYMBOL_REFRESH "  Leer entradas", io_read_cb, NULL);
    io_auto = lv_checkbox_create(row);
    lv_checkbox_set_text(io_auto, "Lectura automática");
    io_status = mk_label(tab, f20, "");
    l = mk_label(tab, f16,
        "Nota: para leer, el CH422G pone todos sus IO como entrada un instante; la "
        "retroiluminación puede parpadear levemente en modo automático.");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_timer_create(io_timer_cb, 300, NULL);
}

// ---------------------------------------------------------------------------
// Pestaña: RS485
// ---------------------------------------------------------------------------
#define RS485_UART UART_NUM_1
static lv_obj_t *rs_log, *rs_baud, *rs_echo;
static volatile bool rs_echo_on;

static void rs485_rx_task(void *arg)
{
    uint8_t buf[128];
    while (1) {
        int n = uart_read_bytes(RS485_UART, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));
        if (n > 0) {
            if (rs_echo_on) uart_write_bytes(RS485_UART, buf, n);
            buf[n] = 0;
            char txt[300];
            int p = snprintf(txt, sizeof(txt), "RX (%d): ", n);
            for (int i = 0; i < n && p < (int)sizeof(txt) - 4; i++) {
                txt[p++] = (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.';
            }
            txt[p] = 0;
            lvgl_lock(-1);
            log_add(rs_log, "%s", txt);
            lvgl_unlock();
        }
    }
}

static void rs_baud_cb(lv_event_t *e)
{
    static const int bauds[] = { 9600, 19200, 38400, 57600, 115200 };
    int b = bauds[lv_dropdown_get_selected(rs_baud)];
    uart_set_baudrate(RS485_UART, b);
    log_add(rs_log, "Velocidad: %d baudios", b);
}

static void rs_send_cb(lv_event_t *e)
{
    static int cnt;
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "Hola desde ESP32-S3 #%d\r\n", ++cnt);
    uart_write_bytes(RS485_UART, msg, n);
    log_add(rs_log, "TX: Hola desde ESP32-S3 #%d", cnt);
}

static void rs_echo_cb(lv_event_t *e)
{
    rs_echo_on = lv_obj_has_state(rs_echo, LV_STATE_CHECKED);
}

static void build_tab_rs485(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_UPLOAD "  RS485");
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(RS485_UART, 2048, 2048, 0, NULL, 0);
    uart_param_config(RS485_UART, &cfg);
    uart_set_pin(RS485_UART, BOARD_RS485_TX, BOARD_RS485_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(rs485_rx_task, "rs485", 4096, NULL, 4, NULL);

    lv_obj_t *l = mk_label(tab, f16, "Conecta A/B a un adaptador USB-RS485 y abre un terminal "
                                     "serie (8N1). Lo que envíes aparecerá aquí.");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_t *row = mk_row(tab);
    rs_baud = lv_dropdown_create(row);
    lv_dropdown_set_options(rs_baud, "9600\n19200\n38400\n57600\n115200");
    lv_dropdown_set_selected(rs_baud, 4);
    lv_obj_add_event_cb(rs_baud, rs_baud_cb, LV_EVENT_VALUE_CHANGED, NULL);
    mk_btn(row, LV_SYMBOL_UPLOAD "  Enviar saludo", rs_send_cb, NULL);
    rs_echo = lv_checkbox_create(row);
    lv_checkbox_set_text(rs_echo, "Eco");
    lv_obj_add_event_cb(rs_echo, rs_echo_cb, LV_EVENT_VALUE_CHANGED, NULL);
    rs_log = mk_log(tab);
}

// ---------------------------------------------------------------------------
// Pestaña: CAN (TWAI)
// ---------------------------------------------------------------------------
static lv_obj_t *can_log, *can_rate, *can_noack, *can_btn_lbl, *can_status;
static volatile bool can_running;
static TaskHandle_t can_task_h;

static void can_rx_task(void *arg)
{
    while (1) {
        if (!can_running) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        twai_message_t m;
        if (twai_receive(&m, pdMS_TO_TICKS(100)) == ESP_OK) {
            char d[40] = "";
            for (int i = 0; i < m.data_length_code && i < 8; i++) {
                sprintf(d + strlen(d), "%02X ", m.data[i]);
            }
            lvgl_lock(-1);
            log_add(can_log, "RX id=0x%03lX%s dlc=%d  %s", (unsigned long)m.identifier,
                    m.extd ? " (ext)" : "", m.data_length_code, d);
            lvgl_unlock();
        }
    }
}

static void can_start_stop_cb(lv_event_t *e)
{
    if (can_running) {
        can_running = false;
        vTaskDelay(pdMS_TO_TICKS(150));
        twai_stop();
        twai_driver_uninstall();
        lv_label_set_text(can_btn_lbl, LV_SYMBOL_PLAY "  Iniciar");
        log_add(can_log, "CAN detenido");
        return;
    }
    static const twai_timing_config_t t125 = TWAI_TIMING_CONFIG_125KBITS();
    static const twai_timing_config_t t250 = TWAI_TIMING_CONFIG_250KBITS();
    static const twai_timing_config_t t500 = TWAI_TIMING_CONFIG_500KBITS();
    static const twai_timing_config_t t1m = TWAI_TIMING_CONFIG_1MBITS();
    const twai_timing_config_t *tt[] = { &t125, &t250, &t500, &t1m };
    bool noack = lv_obj_has_state(can_noack, LV_STATE_CHECKED);
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(BOARD_CAN_TX, BOARD_CAN_RX,
                                                          noack ? TWAI_MODE_NO_ACK : TWAI_MODE_NORMAL);
    g.rx_queue_len = 20;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err = twai_driver_install(&g, tt[lv_dropdown_get_selected(can_rate)], &f);
    if (err == ESP_OK) err = twai_start();
    if (err != ESP_OK) {
        log_add(can_log, "Error al iniciar: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return;
    }
    can_running = true;
    lv_label_set_text(can_btn_lbl, LV_SYMBOL_STOP "  Detener");
    log_add(can_log, "CAN iniciado%s", noack ? " (sin ACK)" : "");
}

static void can_send_cb(lv_event_t *e)
{
    if (!can_running) {
        log_add(can_log, "Primero pulsa Iniciar");
        return;
    }
    static uint8_t cnt;
    twai_message_t m = { .identifier = 0x123, .data_length_code = 8,
                         .data = { 0xCA, 0xFE, 0x00, 0x43, 0x42, 0x00, 0x00, cnt++ } };
    m.self = lv_obj_has_state(can_noack, LV_STATE_CHECKED);
    esp_err_t err = twai_transmit(&m, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        log_add(can_log, "TX id=0x123 dlc=8  CA FE 00 43 42 00 00 %02X", m.data[7]);
    } else {
        log_add(can_log, "TX error: %s", esp_err_to_name(err));
    }
}

static void can_timer_cb(lv_timer_t *t)
{
    if (!can_running) {
        set_status(can_status, lv_palette_main(LV_PALETTE_GREY), "Estado: detenido");
        return;
    }
    twai_status_info_t s;
    if (twai_get_status_info(&s) != ESP_OK) return;
    static const char *st[] = { "parado", "activo", "bus-off", "recuperando" };
    set_status(can_status, s.state == TWAI_STATE_RUNNING ? COL_OK : COL_ERR,
               "Estado: %s   err TX: %lu  err RX: %lu   fallos TX: %lu",
               st[s.state & 3], (unsigned long)s.tx_error_counter,
               (unsigned long)s.rx_error_counter, (unsigned long)s.tx_failed_count);
    if (s.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
}

static void build_tab_can(lv_obj_t *tab)
{
    setup_tab(tab, LV_SYMBOL_LOOP "  CAN");
    lv_obj_t *l = mk_label(tab, f16, "Conecta CANH/CANL a otro nodo con la misma velocidad "
                                     "(y resistencia de 120 Ω). Sin otro nodo, marca \"Sin ACK\".");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_t *row = mk_row(tab);
    can_rate = lv_dropdown_create(row);
    lv_dropdown_set_options(can_rate, "125 kbit/s\n250 kbit/s\n500 kbit/s\n1 Mbit/s");
    lv_dropdown_set_selected(can_rate, 2);
    can_noack = lv_checkbox_create(row);
    lv_checkbox_set_text(can_noack, "Sin ACK");
    lv_obj_t *b = mk_btn(row, LV_SYMBOL_PLAY "  Iniciar", can_start_stop_cb, NULL);
    can_btn_lbl = lv_obj_get_child(b, 0);
    mk_btn(row, LV_SYMBOL_UPLOAD "  Enviar trama", can_send_cb, NULL);
    can_status = mk_label(tab, f16, "");
    can_log = mk_log(tab);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 4, &can_task_h);
    lv_timer_create(can_timer_cb, 1000, NULL);
    can_timer_cb(NULL);
}

// ---------------------------------------------------------------------------
// Construcción general
// ---------------------------------------------------------------------------
static lv_font_t *mk_font(int size, const lv_font_t *fallback)
{
    lv_font_t *f = lv_tiny_ttf_create_data(ttf_start, ttf_end - ttf_start, size);
    if (!f) {
        ESP_LOGE(TAG, "No se pudo cargar la fuente TTF %d", size);
        return (lv_font_t *)fallback;
    }
    f->fallback = fallback;  // símbolos (iconos) desde la fuente integrada
    return f;
}

void ui_create(void)
{
    f16 = mk_font(16, &lv_font_montserrat_16);
    f20 = mk_font(20, &lv_font_montserrat_20);
    f28 = mk_font(28, &lv_font_montserrat_28);
    f64 = mk_font(64, &lv_font_montserrat_28);

    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_TEAL), false, f20);
    lv_disp_set_theme(disp, th);

    lv_obj_t *tv = lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, 150);
    lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_text_font(btns, f20, 0);
    lv_obj_set_style_bg_color(btns, lv_color_hex(0x263238), 0);
    lv_obj_set_style_text_color(btns, lv_color_white(), 0);
    lv_obj_clear_flag(lv_tabview_get_content(tv), LV_OBJ_FLAG_SCROLLABLE);

    build_tab_system(lv_tabview_add_tab(tv, "Sistema"));
    build_tab_touch(lv_tabview_add_tab(tv, "Táctil"));
    build_tab_display(lv_tabview_add_tab(tv, "Pantalla"));
    build_tab_wifi(lv_tabview_add_tab(tv, "WiFi"));
    build_tab_rtc(lv_tabview_add_tab(tv, "Reloj"));
    build_tab_sd(lv_tabview_add_tab(tv, "microSD"));
    build_tab_io(lv_tabview_add_tab(tv, "E/S"));
    build_tab_rs485(lv_tabview_add_tab(tv, "RS485"));
    build_tab_can(lv_tabview_add_tab(tv, "CAN"));
}
