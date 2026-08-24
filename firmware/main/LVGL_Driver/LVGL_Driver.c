#include "LVGL_Driver.h"

#include "LCD_Driver.h"
#include "Touch_Driver.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_Driver";

#define TICK_PERIOD_MS   2
#define TASK_STACK       6144
#define TASK_PRIO        4
/* Dos buffers parciales en RAM interna: el DMA del SPI va mas rapido desde ahi
 * que desde PSRAM, y 240x40 cabe de sobra en la transferencia maxima del bus. */
#define BUF_LINES        40
#define BUF_PIXELS       (LCD_H_RES * BUF_LINES)

static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;
static SemaphoreHandle_t s_mutex;
static volatile uint32_t s_flush_count;

bool LVGL_Lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_mutex, ticks) == pdTRUE;
}

void LVGL_Unlock(void)
{
    xSemaphoreGiveRecursive(s_mutex);
}

uint32_t LVGL_TakeFlushCount(void)
{
    const uint32_t n = s_flush_count;
    s_flush_count = 0;
    return n;
}

/* Lo llama LCD_Driver desde la interrupcion de fin de DMA. */
static void on_flush_done(void)
{
    lv_disp_flush_ready(&s_disp_drv);
    s_flush_count++;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    /* LVGL da coordenadas inclusivas; esp_lcd las quiere exclusivas por la derecha. */
    esp_lcd_panel_draw_bitmap(LCD_GetPanel(), area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint8_t n = 0;

    esp_lcd_touch_read_data(Touch_GetHandle());
    const bool pulsado = esp_lcd_touch_get_coordinates(Touch_GetHandle(), x, y, NULL, &n, 1);

    if (pulsado && n > 0) {
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
        uint32_t espera_ms = 10;
        if (LVGL_Lock(-1)) {
            espera_ms = lv_timer_handler();
            LVGL_Unlock();
        }
        if (espera_ms > 50) {
            espera_ms = 50;
        } else if (espera_ms < 5) {
            espera_ms = 5;
        }
        vTaskDelay(pdMS_TO_TICKS(espera_ms));
    }
}

esp_err_t LVGL_Init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex");

    lv_init();

    lv_color_t *buf1 = heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "sin RAM para los buffers de LVGL");

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_H_RES;
    s_disp_drv.ver_res = LCD_V_RES;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &draw_buf;
    ESP_RETURN_ON_FALSE(lv_disp_drv_register(&s_disp_drv) != NULL, ESP_FAIL, TAG, "disp_drv");

    LCD_SetTransDoneHook(on_flush_done);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    ESP_RETURN_ON_FALSE(lv_indev_drv_register(&s_indev_drv) != NULL, ESP_FAIL, TAG, "indev_drv");

    const esp_timer_create_args_t tick_args = {.callback = tick_cb, .name = "lv_tick"};
    esp_timer_handle_t tick;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick), TAG, "tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick, TICK_PERIOD_MS * 1000), TAG, "tick start");

    ESP_RETURN_ON_FALSE(
        xTaskCreate(lvgl_task, "lvgl", TASK_STACK, NULL, TASK_PRIO, NULL) == pdPASS,
        ESP_FAIL, TAG, "tarea lvgl");

    ESP_LOGI(TAG, "LVGL %d.%d.%d listo, buffers de %d px x2 en RAM interna",
             lv_version_major(), lv_version_minor(), lv_version_patch(), BUF_PIXELS);
    return ESP_OK;
}
