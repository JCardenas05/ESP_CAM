#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "board_pins.h"

/* Levanta el bus I2C0 y el controlador tactil CST816S.
 * El mismo bus lo comparte la IMU QMI8658. */
esp_err_t Touch_Init(void);

esp_lcd_touch_handle_t Touch_GetHandle(void);
