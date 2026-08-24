#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Pantalla: cuadro de imagen de 240x240 arriba y una barra de control de 80 px
 * abajo. El cuadro es cuadrado porque es lo que genera el modelo de imagen. */

/* Crea la interfaz. Los dos buffers son de LCD_IMG_SIZE x LCD_IMG_SIZE pixeles
 * en RGB565 con los bytes girados, que es justo el formato que dan la camara,
 * el servidor y LVGL con LV_COLOR_16_SWAP. */
void UI_Init(uint8_t *buffer_preview, uint8_t *buffer_original, uint8_t *buffer_resultado);

/* Nombre del estilo elegido, tal cual viaja al servidor ("anime", "cartoon"...). */
const char *UI_EstiloActual(void);

/* Devuelve true una sola vez por pulsacion del boton de disparo. */
bool UI_TomarPeticionCaptura(void);

/* Devuelve true una sola vez cuando el usuario pide volver al visor. */
bool UI_TomarPeticionVolver(void);

void UI_MostrarPreview(void);
void UI_RefrescarPreview(void);
void UI_MostrarOcupado(const char *texto);
void UI_ActualizarOcupado(const char *texto);

/* Ajusta la barra de espera con lo que tardo la ultima foto que salio bien. Sin
 * llamarla, la barra se reparte sobre una estimacion de 30 s. */
void UI_CalibrarEspera(uint32_t milisegundos);
void UI_MostrarResultado(void);

/* Repinta el cuadro con lo que haya en el buffer de resultado, sin tocar la
 * vista. Es lo que se llama con cada borrador que va llegando del servidor. */
void UI_RefrescarResultado(void);
void UI_MostrarError(const char *texto);

/* Indicadores del HUD, sobre la esquina de la imagen: verde si va, rojo si no.
 * El WiFi y el servidor son fallos distintos y se ven por separado — con una
 * sola luz no se sabe si hay que mirar el router o el PC. */
void UI_EstadoWifi(bool ok);
void UI_EstadoServidor(bool ok);
