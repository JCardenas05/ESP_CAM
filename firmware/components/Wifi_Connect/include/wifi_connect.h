//Component/wifi_connect.h
#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi.h"

/* Se invoca en cada cambio de estado del WiFi (event_id de WIFI_EVENT o el
 * IP_EVENT_STA_GOT_IP), para que la UI pueda reflejarlo. */
typedef void (*callback_event_t)(uint32_t event_id);

/**
 * @brief Conecta en modo estacion y espera a tener IP.
 * @param ssid            Nombre de la red.
 * @param pass            Contrasena; cadena vacia para redes abiertas.
 * @param timeout_ms      Tiempo maximo de espera.
 * @param callback_event  Puede ser NULL.
 * @return ESP_OK si obtuvo IP, ESP_ERR_TIMEOUT si se agoto la espera.
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *pass, int timeout_ms,
                           callback_event_t callback_event);

/** @return La IP como texto, o "0.0.0.0" si todavia no hay. */
const char *wifi_get_ip_str(void);

bool wifi_is_connected(void);

#endif // WIFI_CONNECT_H
