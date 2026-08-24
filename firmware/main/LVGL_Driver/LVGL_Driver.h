#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/* Arranca LVGL sobre la LCD y el tactil, y lanza su tarea.
 * Requiere LCD_Init() y Touch_Init() previos. */
esp_err_t LVGL_Init(void);

/* LVGL no es thread-safe: hay que tomar el mutex antes de tocar cualquier
 * objeto desde una tarea que no sea la suya. timeout_ms < 0 = esperar siempre. */
bool LVGL_Lock(int timeout_ms);
void LVGL_Unlock(void);

/* Fotogramas volcados a la pantalla desde la ultima llamada. Sirve para medir
 * FPS reales sin depender del monitor de rendimiento de LVGL. */
uint32_t LVGL_TakeFlushCount(void);
