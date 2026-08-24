#include "http_client.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "http_client";

#ifdef CONFIG_ESP_CAM_AUTH_TOKEN
#define TOKEN CONFIG_ESP_CAM_AUTH_TOKEN
#else
#define TOKEN ""
#endif

/* Un unico cliente para todas las peticiones.
 *
 * Abrir la conexion cuesta ~880 ms en esta red, y se pagaba en cada foto. Si el
 * handle no se cierra, el siguiente esp_http_client_open reutiliza el socket:
 * por dentro llama a prepare(), que rearma la linea de peticion, pero solo
 * conecta si el estado es menor que CONNECTED.
 *
 * Quien lo mantiene caliente es la comprobacion de salud cada 20 s. Para que
 * sirva de algo, uvicorn tiene que arrancar con --timeout-keep-alive por encima
 * de ese intervalo: con los 5 s de por defecto el servidor cierra la conexion
 * antes de que llegue la siguiente y no se gana nada. */
static esp_http_client_handle_t s_cliente;
static SemaphoreHandle_t s_mutex;

esp_err_t http_client_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Deja el cliente listo para una peticion nueva sobre el socket que ya hubiera.
 * El Content-Type se borra siempre: se queda pegado de una peticion a otra y
 * mandaria el multipart de una foto en la cabecera de un GET. */
static esp_err_t cliente_preparar(const char *url, esp_http_client_method_t metodo, int timeout_ms)
{
    if (s_cliente == NULL) {
        const esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = timeout_ms,
            .keep_alive_enable = true,
            /* Necesario desde que el proxy vive detras de Cloudflare y se habla
             * por https. El bundle son las CA publicas que trae ESP-IDF, las
             * mismas que usa el firmware de Pomodoro contra esta misma VM.
             * Con http:// el campo se ignora, asi que no estorba en la LAN. */
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        s_cliente = esp_http_client_init(&cfg);
        ESP_RETURN_ON_FALSE(s_cliente != NULL, ESP_FAIL, TAG, "no se pudo crear el cliente HTTP");
    }
    ESP_RETURN_ON_ERROR(esp_http_client_set_url(s_cliente, url), TAG, "fijar URL");
    ESP_RETURN_ON_ERROR(esp_http_client_set_method(s_cliente, metodo), TAG, "fijar metodo");
    esp_http_client_set_timeout_ms(s_cliente, timeout_ms);
    esp_http_client_delete_header(s_cliente, "Content-Type");

    /* El proxy publicado en internet exige este token en /stylize: sin el,
     * cualquiera que de con la URL gasta el saldo de OpenAI. Se manda tambien
     * en /health, que no lo pide, por no llevar dos caminos distintos. */
    if (TOKEN[0] != '\0') {
        esp_http_client_set_header(s_cliente, "Authorization", "Bearer " TOKEN);
    }
    return ESP_OK;
}

/* Tras un fallo el socket queda en un estado que no se puede reaprovechar: se
 * cierra para que la siguiente peticion vuelva a conectar de cero. */
static void cliente_invalidar(void)
{
    if (s_cliente != NULL) {
        esp_http_client_close(s_cliente);
    }
}

esp_err_t http_health_check(const char *base_url, int timeout_ms, char *body, size_t body_len)
{
    ESP_RETURN_ON_FALSE(base_url != NULL && base_url[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "URL del servidor vacia: configurala con 'idf.py menuconfig'");

    char url[160];
    snprintf(url, sizeof(url), "%s/health", base_url);

    if (body != NULL && body_len > 0) {
        body[0] = '\0';
    }
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "falta http_client_init()");

    /* Si hay una foto en marcha no se espera: la comprobacion puede saltarse un
     * ciclo, pero bloquear aqui retrasaria el disparo. */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGD(TAG, "cliente ocupado, me salto la comprobacion");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    size_t leidos = 0;
    ESP_GOTO_ON_ERROR(cliente_preparar(url, HTTP_METHOD_GET, timeout_ms), fin, TAG, "preparar cliente");
    ESP_GOTO_ON_ERROR(esp_http_client_open(s_cliente, 0), fin, TAG, "abrir conexion");

    esp_http_client_fetch_headers(s_cliente);
    const int status = esp_http_client_get_status_code(s_cliente);

    /* El cuerpo hay que drenarlo entero aunque no interese: si queda un byte
     * sin leer, la siguiente peticion sobre este socket lee la cola de esta. */
    while (1) {
        char trozo[64];
        const int n = esp_http_client_read(s_cliente, trozo, sizeof(trozo));
        if (n <= 0) {
            break;
        }
        if (body != NULL && leidos + (size_t)n < body_len) {
            memcpy(body + leidos, trozo, (size_t)n);
            leidos += (size_t)n;
            body[leidos] = '\0';
        }
    }

    if (status != 200) {
        ESP_LOGE(TAG, "GET %s respondio HTTP %d", url, status);
        ret = ESP_FAIL;
        goto fin;
    }
    ESP_LOGI(TAG, "GET %s -> 200, %u bytes: %s", url, (unsigned)leidos,
             (body != NULL) ? body : "(sin cuerpo)");

fin:
    if (ret != ESP_OK) {
        cliente_invalidar();
    }
    xSemaphoreGive(s_mutex);
    return ret;
}

#define FRONTERA      "----esp32camboundary7Nk3"
#define TROZO_SUBIDA  1460   /* un MSS: evita fragmentar dentro de lwIP */

esp_err_t http_post_photo(const char *base_url, const char *style,
                          const char *capture_id,
                          const uint8_t *jpeg, size_t jpeg_len,
                          uint8_t *out, size_t out_cap, size_t *out_len,
                          int timeout_ms,
                          http_frame_cb_t on_frame, void *user)
{
    ESP_RETURN_ON_FALSE(base_url && base_url[0] && jpeg && jpeg_len && out && out_cap,
                        ESP_ERR_INVALID_ARG, TAG, "argumentos invalidos");
    *out_len = 0;

    char url[192];
    snprintf(url, sizeof(url), "%s/%s", base_url,
         strcmp(style, "test") == 0 ? "echo" : "stylize");

    char cabecera[512];
    const int cabecera_len = snprintf(cabecera, sizeof(cabecera),
        "--" FRONTERA "\r\n"
        "Content-Disposition: form-data; name=\"style\"\r\n\r\n"
        "%s\r\n"
        "--" FRONTERA "\r\n"
        "Content-Disposition: form-data; name=\"capture_id\"\r\n\r\n"
        "%s\r\n"
        "--" FRONTERA "\r\n"
        "Content-Disposition: form-data; name=\"image\"; filename=\"foto.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n", style, capture_id ? capture_id : "");

    char cierre[64];
    const int cierre_len = snprintf(cierre, sizeof(cierre), "\r\n--" FRONTERA "--\r\n");

    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "falta http_client_init()");
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;
    esp_http_client_handle_t cliente = s_cliente;
    ESP_GOTO_ON_ERROR(cliente_preparar(url, HTTP_METHOD_POST, timeout_ms), fin, TAG, "preparar cliente");
    cliente = s_cliente;
    esp_http_client_set_header(cliente, "Content-Type",
                               "multipart/form-data; boundary=" FRONTERA);

    const int total = cabecera_len + (int)jpeg_len + cierre_len;
    ESP_LOGI(TAG, "antes de conectar: interna %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    const int64_t t_open = esp_timer_get_time();
    ESP_GOTO_ON_ERROR(esp_http_client_open(cliente, total), fin, TAG, "abrir conexion");
    ESP_LOGI(TAG, "conexion abierta en %" PRId64 " ms", (esp_timer_get_time() - t_open) / 1000);

    ESP_GOTO_ON_FALSE(esp_http_client_write(cliente, cabecera, cabecera_len) == cabecera_len,
                      ESP_FAIL, fin, TAG, "escribir cabecera multipart");

    /* En trozos: esp_http_client_write puede escribir menos de lo pedido. */
    const int64_t t_subida = esp_timer_get_time();
    size_t hito = 0;
    for (size_t enviados = 0; enviados < jpeg_len; ) {
        const size_t pendiente = jpeg_len - enviados;
        const int n = esp_http_client_write(cliente, (const char *)jpeg + enviados,
                                            pendiente > TROZO_SUBIDA ? TROZO_SUBIDA : (int)pendiente);
        ESP_GOTO_ON_FALSE(n > 0, ESP_FAIL, fin, TAG,
                          "subiendo el JPEG (%u/%u, interna %u KB)",
                          (unsigned)enviados, (unsigned)jpeg_len,
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        enviados += (size_t)n;
        if (enviados - hito >= 8192) {
            hito = enviados;
            ESP_LOGI(TAG, "subido %u/%u en %" PRId64 " ms, interna %u KB",
                     (unsigned)enviados, (unsigned)jpeg_len,
                     (esp_timer_get_time() - t_subida) / 1000,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        }
    }
    ESP_LOGI(TAG, "JPEG subido entero en %" PRId64 " ms",
             (esp_timer_get_time() - t_subida) / 1000);

    ESP_GOTO_ON_FALSE(esp_http_client_write(cliente, cierre, cierre_len) == cierre_len,
                      ESP_FAIL, fin, TAG, "escribir cierre multipart");

    const int longitud = esp_http_client_fetch_headers(cliente);
    const int status = esp_http_client_get_status_code(cliente);
    if (status != 200) {
        char detalle[192] = {0};
        const int n = esp_http_client_read(cliente, detalle, sizeof(detalle) - 1);
        ESP_LOGE(TAG, "POST %s respondio HTTP %d: %s", url, status, n > 0 ? detalle : "(sin cuerpo)");
        ret = ESP_FAIL;
        goto fin;
    }
    /* La respuesta ya no es un bloque unico: son varios frames de out_cap bytes
     * encadenados, uno por cada borrador que suelta el modelo mas el definitivo.
     * Como el numero no se sabe de antemano viene en chunked, sin
     * Content-Length, y el final lo marca el cierre de la conexion. */
    (void)longitud;
    const int64_t t_frames = esp_timer_get_time();
    size_t en_curso = 0;
    int frames = 0;
    while (1) {
        const int n = esp_http_client_read(cliente, (char *)out + en_curso, out_cap - en_curso);
        if (n <= 0) {
            break;
        }
        en_curso += (size_t)n;
        if (en_curso < out_cap) {
            continue;
        }
        ESP_LOGI(TAG, "frame %d completo a los %" PRId64 " ms", frames,
                 (esp_timer_get_time() - t_frames) / 1000);
        if (on_frame != NULL) {
            on_frame(out, en_curso, frames, user);
        }
        frames++;
        en_curso = 0;
    }

    /* Si el ultimo frame llego a medias no cuenta: out_len se queda en lo que
     * de verdad hay entero en el buffer para que quien llama lo detecte. */
    *out_len = frames > 0 && en_curso == 0 ? out_cap : en_curso;
    if (frames == 0) {
        ESP_LOGE(TAG, "no llego ningun frame completo (%u bytes sueltos)", (unsigned)en_curso);
        ret = ESP_ERR_INVALID_SIZE;
        goto fin;
    }
    ESP_LOGI(TAG, "POST %s -> 200, %d frames de %u bytes", url, frames, (unsigned)out_cap);

fin:
    /* En el camino bueno NO se cierra: dejar el socket vivo es lo que ahorra el
     * handshake de la proxima foto. Solo se tira si algo fallo. */
    if (ret != ESP_OK) {
        cliente_invalidar();
    }
    xSemaphoreGive(s_mutex);
    return ret;
}
