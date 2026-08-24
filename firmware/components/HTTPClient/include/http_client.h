#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Pide GET {base_url}/health al proxy.
 *
 * Es la comprobacion de vida del servidor: si esto responde, el camino
 * ESP32 -> WiFi -> LAN -> FastAPI esta completo.
 *
 * @param base_url    Por ejemplo "http://192.168.1.104:8000".
 * @param timeout_ms  Tiempo maximo de la peticion.
 * @param body        Buffer donde dejar el cuerpo de la respuesta; puede ser NULL.
 * @param body_len    Tamano del buffer.
 * @return ESP_OK solo si el codigo HTTP es 200.
 */
/**
 * @brief Prepara el cliente HTTP compartido. Llamar una vez antes que nada.
 *
 * Todas las peticiones reutilizan el mismo socket, asi que hace falta un mutex
 * creado antes de que ninguna tarea empiece a pedir.
 */
esp_err_t http_client_init(void);

esp_err_t http_health_check(const char *base_url, int timeout_ms, char *body, size_t body_len);

/**
 * @brief Sube el JPEG a {base_url}/stylize y recibe la imagen ya estilizada.
 *
 * Manda multipart/form-data con dos campos, "style" e "image", que es lo que
 * espera FastAPI. La respuesta son bytes RGB565 big-endian en crudo: el ESP32
 * no decodifica nada, los escribe tal cual en el panel.
 *
 * @param base_url    Por ejemplo "http://192.168.1.14:8000".
 * @param style       "anime", "cartoon", "sketch", "watercolor" o "pixel".
 * @param capture_id  Identificador de ESTA captura. Debe repetirse en los
 *                    reintentos: el servidor devuelve entonces el resultado ya
 *                    calculado en vez de volver a pagar otra generacion.
 * @param jpeg        Foto tal cual sale de la camara.
 * @param out         Buffer de destino; debe estar en PSRAM (son ~115 KB).
 * @param out_cap     Capacidad del buffer.
 * @param out_len     Bytes del ultimo frame recibido.
 * @param on_frame    Se llama con cada frame completo segun va llegando; puede
 *                    ser NULL. La respuesta trae varios: el modelo suelta uno o
 *                    dos borradores antes del definitivo, y pintarlos segun
 *                    llegan baja la espera en blanco de ~20 s a ~7 s.
 * @param user        Se pasa tal cual a on_frame.
 * @param timeout_ms  Generoso: el modelo tarda mas de 30 s en responder.
 */
/**
 * @brief Aviso de que hay un frame completo en el buffer, listo para pintar.
 *
 * @param indice  0 para el primer borrador, y va subiendo. El ultimo que llega
 *                es el resultado definitivo.
 */
typedef void (*http_frame_cb_t)(const uint8_t *frame, size_t len, int indice, void *user);

esp_err_t http_post_photo(const char *base_url, const char *style,
                          const char *capture_id,
                          const uint8_t *jpeg, size_t jpeg_len,
                          uint8_t *out, size_t out_cap, size_t *out_len,
                          int timeout_ms,
                          http_frame_cb_t on_frame, void *user);

#endif // HTTP_CLIENT_H
