/* Pinout de la Waveshare ESP32-S3-Touch-LCD-2.
 * Fuente: demo oficial reference/waveshare-demos/05_lvgl_camera.
 * Unica fuente de verdad para pines: no repetir estos numeros en otro sitio. */
#pragma once

/* --- LCD ST7789T3, SPI2, 240x320 --- */
#define LCD_SPI_HOST     SPI2_HOST
#define BOARD_PIN_LCD_SCLK     39
#define BOARD_PIN_LCD_MOSI     38
#define BOARD_PIN_LCD_MISO     40
#define BOARD_PIN_LCD_CS       45
#define BOARD_PIN_LCD_DC       42
#define BOARD_PIN_LCD_RST      (-1)   /* sin pin de reset: se resetea por comando */
#define BOARD_PIN_LCD_BL       1      /* retroiluminacion por PWM (LEDC) */

#define LCD_H_RES        240
#define LCD_V_RES        320
#define LCD_PCLK_HZ      (80 * 1000 * 1000)

/* --- Touch CST816S e IMU QMI8658, I2C0 --- */
#define BOARD_I2C_NUM          0
#define BOARD_PIN_I2C_SDA      48
#define BOARD_PIN_I2C_SCL      47

/* --- Camara OV5640, DVP 8 bits --- */
#define BOARD_PIN_CAM_PWDN     17
#define BOARD_PIN_CAM_RESET    (-1)
#define BOARD_PIN_CAM_XCLK     8
#define BOARD_PIN_CAM_SIOD     21
#define BOARD_PIN_CAM_SIOC     16
#define BOARD_PIN_CAM_VSYNC    6
#define BOARD_PIN_CAM_HREF     4
#define BOARD_PIN_CAM_PCLK     9
#define BOARD_PIN_CAM_D7       2
#define BOARD_PIN_CAM_D6       7
#define BOARD_PIN_CAM_D5       10
#define BOARD_PIN_CAM_D4       14
#define BOARD_PIN_CAM_D3       11
#define BOARD_PIN_CAM_D2       15
#define BOARD_PIN_CAM_D1       13
#define BOARD_PIN_CAM_D0       12
