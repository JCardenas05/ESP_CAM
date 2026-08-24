#pragma once

#include "esp_camera.h"
#include "esp_err.h"
#include "board_pins.h"

/* La camara trabaja en dos modos y cambiar entre ellos reinicia el driver:
 *  - PREVIEW: RGB565 320x240 (QVGA).
 *  - PHOTO:   JPEG 800x600 (SVGA), que es lo que se sube al servidor.
 *
 * Los dos son 4:3, y eso importa: el OV5640 usa una ventana de sensor distinta
 * por cada relacion de aspecto. Las 4:3 abren 2560x1920 (campo completo); las
 * 3:2 abren 2560x1704, recortando arriba y abajo. Con ambos modos en 4:3, el
 * preview y la foto ven exactamente lo mismo. */
typedef enum {
    CAMERA_MODE_PREVIEW,
    CAMERA_MODE_PHOTO,
} camera_mode_t;

esp_err_t Camera_Init(void);

/* Reinicia el driver con la configuracion del modo pedido (~200 ms). */
esp_err_t Camera_SetMode(camera_mode_t mode);

/* Apaga el driver por completo y libera sus frame buffers.
 * Hay que hacerlo antes de subir la foto: con la camara capturando, su DMA
 * y su tarea no dejan al WiFi atender los ACK a tiempo, lwIP agota los
 * reintentos y aborta la conexion a mitad de la subida. */
esp_err_t Camera_Stop(void);

bool Camera_IsRunning(void);

camera_mode_t Camera_GetMode(void);

#define CAMERA_PREVIEW_W   320
#define CAMERA_PREVIEW_H   240
