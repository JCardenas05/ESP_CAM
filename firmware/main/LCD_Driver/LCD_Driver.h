#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "board_pins.h"

/* El ST7789 espera RGB565 big-endian, asi que los colores se guardan con los
 * bytes intercambiados respecto al orden nativo del ESP32 (little-endian). */
static inline uint16_t LCD_RGB565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

/* Area util de imagen: cuadrada, pegada arriba, con una barra de control de
 * 80 px debajo. Es cuadrada porque los modelos de imagen generan cuadrado
 * (1024x1024), asi que lo que devuelve la IA encaja sin recortar ni un pixel. */
#define LCD_IMG_SIZE   LCD_H_RES
#define LCD_IMG_Y0     0

esp_err_t LCD_Init(void);

/* Nivel de retroiluminacion, 0-100 %. */
esp_err_t LCD_SetBrightness(uint8_t percent);

/* Pinta toda la pantalla de un color (ya en formato LCD_RGB565). */
esp_err_t LCD_Fill(uint16_t color);

/* Rectangulo relleno. x2/y2 son exclusivos, igual que en esp_lcd. */
esp_err_t LCD_FillRect(int x1, int y1, int x2, int y2, uint16_t color);

/* Vuelca un bloque de pixeles y espera a que el DMA termine, de modo que el
 * llamante puede reutilizar su buffer nada mas volver. */
esp_err_t LCD_DrawBitmap(int x1, int y1, int x2, int y2, const void *pixels);

/* Se invoca desde la interrupcion cuando el DMA acaba de volcar. LVGL lo usa
 * para dar por terminado su flush; si hay hook instalado, LCD_DrawBitmap deja
 * de bloquear porque el dueño del panel pasa a ser LVGL. */
typedef void (*lcd_trans_done_hook_t)(void);
void LCD_SetTransDoneHook(lcd_trans_done_hook_t hook);

/* Handle del panel para las etapas que dibujen directamente (camara, LVGL). */
esp_lcd_panel_handle_t LCD_GetPanel(void);

/* Handle del bus: LVGL lo necesita para enterarse de cuando termina el DMA. */
esp_lcd_panel_io_handle_t LCD_GetPanelIO(void);

/* Transferencia SPI maxima configurada, en pixeles. Ningun buffer de dibujo
 * (incluidos los de LVGL) puede superarla de una sola vez. */
#define LCD_MAX_TRANSFER_PIXELS  (LCD_H_RES * 80)
