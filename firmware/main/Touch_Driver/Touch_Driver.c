#include "Touch_Driver.h"

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"

static const char *TAG = "Touch_Driver";

static esp_lcd_touch_handle_t s_touch;

esp_lcd_touch_handle_t Touch_GetHandle(void)
{
    return s_touch;
}

esp_err_t Touch_Init(void)
{
    /* Driver I2C **legacy** a proposito. esp32-camera solo trae su SCCB sobre el
     * driver nuevo a partir de ESP-IDF 5.4; en 5.3.1 compila driver/sccb.c, que
     * usa el legacy. ESP-IDF prohibe mezclar ambos en el mismo binario (aborta
     * en un constructor, antes de app_main), asi que todo el I2C va por el viejo. */
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_PIN_I2C_SDA,
        .scl_io_num = BOARD_PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_NUM, &i2c_cfg), TAG, "i2c config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(BOARD_I2C_NUM, i2c_cfg.mode, 0, 0, 0), TAG, "i2c install");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    /* La macro rellena scl_speed_hz, que el camino legacy rechaza con un assert:
     * la velocidad ya viene fijada en i2c_param_config. */
    io_cfg.scl_speed_hz = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v1(BOARD_I2C_NUM, &io_cfg, &io), TAG, "touch io");

    /* x_max/y_max en unidades de pantalla: 240 de ancho por 320 de alto.
     * Validado tocando las 4 esquinas: no hacen falta swap ni mirror. */
    const esp_lcd_touch_config_t cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(io, &cfg, &s_touch), TAG, "cst816s");

    ESP_LOGI(TAG, "CST816S listo en I2C%d (SDA %d, SCL %d)",
             BOARD_I2C_NUM, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL);
    return ESP_OK;
}
