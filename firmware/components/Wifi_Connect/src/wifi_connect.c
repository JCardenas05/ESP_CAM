#include "wifi_connect.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_connect";

#define BIT_CONECTADO  BIT0
#define BIT_FALLO      BIT1
#define REINTENTOS_MAX 5

static EventGroupHandle_t s_eventos;
static callback_event_t s_callback;
static char s_ip[16] = "0.0.0.0";
static int s_reintentos;
static bool s_conectado;

bool wifi_is_connected(void)
{
    return s_conectado;
}

const char *wifi_get_ip_str(void)
{
    return s_ip;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_conectado = false;
        strcpy(s_ip, "0.0.0.0");
        if (s_reintentos < REINTENTOS_MAX) {
            s_reintentos++;
            ESP_LOGW(TAG, "desconectado, reintento %d/%d", s_reintentos, REINTENTOS_MAX);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_eventos, BIT_FALLO);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_reintentos = 0;
        s_conectado = true;
        ESP_LOGI(TAG, "conectado, IP %s", s_ip);
        xEventGroupSetBits(s_eventos, BIT_CONECTADO);
    }

    if (s_callback != NULL) {
        s_callback((uint32_t)id);
    }
}

esp_err_t wifi_connect_sta(const char *ssid, const char *pass, int timeout_ms,
                           callback_event_t callback_event)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "SSID vacio: configuralo con 'idf.py menuconfig'");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    s_callback = callback_event;
    s_eventos = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_eventos != NULL, ESP_ERR_NO_MEM, TAG, "event group");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL), TAG, "handler wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL), TAG, "handler ip");

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (pass != NULL) {
        strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    }
    cfg.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "modo sta");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    /* Sin esto el modem duerme entre balizas y las subidas sostenidas se
     * atascan: los ACK del servidor llegan tarde, lwIP agota reintentos y
     * aborta la conexion. Cuesta bateria, pero subir una foto lo necesita. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "desactivar power save");

    ESP_LOGI(TAG, "conectando a \"%s\"...", ssid);
    const EventBits_t bits = xEventGroupWaitBits(s_eventos, BIT_CONECTADO | BIT_FALLO,
                                                 pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONECTADO) {
        return ESP_OK;
    }
    if (bits & BIT_FALLO) {
        ESP_LOGE(TAG, "no se pudo conectar a \"%s\" tras %d intentos", ssid, REINTENTOS_MAX);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "timeout conectando a \"%s\"", ssid);
    return ESP_ERR_TIMEOUT;
}
