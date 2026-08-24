#include "LCD_Driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "LCD_Driver";

#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_DUTY_RES       LEDC_TIMER_10_BIT
#define BL_DUTY_MAX       ((1 << 10) - 1)
#define BL_FREQ_HZ        10000

/* Se pinta por franjas para no reservar los 150 KB de un frame entero:
 * el buffer va en RAM interna porque el SPI lo consume por DMA. */
#define STRIPE_LINES      40
#define STRIPE_PIXELS     (LCD_H_RES * STRIPE_LINES)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_tx_done;
static lcd_trans_done_hook_t s_hook;
static uint16_t *s_stripe;

esp_lcd_panel_handle_t LCD_GetPanel(void)
{
    return s_panel;
}

esp_lcd_panel_io_handle_t LCD_GetPanelIO(void)
{
    return s_io;
}

void LCD_SetTransDoneHook(lcd_trans_done_hook_t hook)
{
    s_hook = hook;
}

/* Contexto de interrupcion. esp_lcd solo la dispara en el ultimo trozo de cada
 * draw_bitmap, aunque lo haya partido en varias transacciones SPI. */
static bool on_trans_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_tx_done, &hp);
    if (s_hook != NULL) {
        s_hook();
    }
    return hp == pdTRUE;
}

esp_err_t LCD_DrawBitmap(int x1, int y1, int x2, int y2, const void *pixels)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels),
                        TAG, "draw_bitmap");
    if (s_hook == NULL) {
        xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(1000));
    }
    return ESP_OK;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = BL_DUTY_RES,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    const ledc_channel_config_t channel = {
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_PIN_LCD_BL,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel);
}

esp_err_t LCD_SetBrightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t duty = (percent * BL_DUTY_MAX) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty), TAG, "set duty");
    return ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

esp_err_t LCD_Init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    const spi_bus_config_t buscfg = {
        .sclk_io_num = BOARD_PIN_LCD_SCLK,
        .mosi_io_num = BOARD_PIN_LCD_MOSI,
        .miso_io_num = BOARD_PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_MAX_TRANSFER_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_PIN_LCD_DC,
        .cs_gpio_num = BOARD_PIN_LCD_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io),
        TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "st7789");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "swap_xy");
    /* El panel de esta placa es de los que necesitan inversion: sin esto los
     * colores salen en negativo. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_tx_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_tx_done != NULL, ESP_ERR_NO_MEM, TAG, "semaforo");
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL),
                        TAG, "callbacks");

    s_stripe = heap_caps_malloc(STRIPE_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_stripe != NULL, ESP_ERR_NO_MEM, TAG, "sin RAM para el buffer de franja");

    ESP_LOGI(TAG, "ST7789 %dx%d listo en SPI%d a %d MHz",
             LCD_H_RES, LCD_V_RES, LCD_SPI_HOST + 1, LCD_PCLK_HZ / 1000000);
    return ESP_OK;
}

esp_err_t LCD_FillRect(int x1, int y1, int x2, int y2, uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "display sin inicializar");
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > LCD_H_RES) x2 = LCD_H_RES;
    if (y2 > LCD_V_RES) y2 = LCD_V_RES;
    if (x1 >= x2 || y1 >= y2) {
        return ESP_OK;
    }

    const int ancho = x2 - x1;
    const int lineas_max = STRIPE_PIXELS / ancho;

    for (int i = 0; i < ancho * lineas_max; i++) {
        s_stripe[i] = color;
    }
    for (int y = y1; y < y2; y += lineas_max) {
        const int lineas = (y + lineas_max > y2) ? (y2 - y) : lineas_max;
        ESP_RETURN_ON_ERROR(LCD_DrawBitmap(x1, y, x2, y + lineas, s_stripe), TAG, "franja");
    }
    return ESP_OK;
}

esp_err_t LCD_Fill(uint16_t color)
{
    return LCD_FillRect(0, 0, LCD_H_RES, LCD_V_RES, color);
}
