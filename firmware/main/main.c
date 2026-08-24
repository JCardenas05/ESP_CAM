/* ESP_CAM — Etapa 8: camara de bolsillo con interfaz.
 * Visor en vivo, selector de estilo, espera con progreso, resultado de la IA y
 * comparacion con el original. */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Camera_Driver.h"
#include "LCD_Driver.h"
#include "LVGL_Driver.h"
#include "Touch_Driver.h"
#include "UI/ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "http_client.h"
#include "wifi_connect.h"

static const char *TAG = "esp_cam";

#define IMG_PIXELES      (LCD_IMG_SIZE * LCD_IMG_SIZE)
#define IMG_BYTES        (IMG_PIXELES * 2)
#define JPEG_MAX         (128 * 1024)
#define INTENTOS_MAX     3
/* Con 1, las fotos van a /echo en vez de a la IA: mismo camino de red, coste cero. */
#define MODO_DIAGNOSTICO 0

static uint16_t *s_preview;    /* lo que ve el visor */
static uint16_t *s_original;   /* congelado en el momento del disparo */
static uint8_t *s_resultado;   /* lo que devuelve la IA */
static uint8_t *s_jpeg;        /* la foto, ya a salvo del apagado de la camara */

/* Lo mira la tarea de salud para no competir por la red durante una captura. */
static volatile bool s_capturando;

static uint16_t s_lut_col[LCD_IMG_SIZE];
static uint16_t s_lut_fila[LCD_IMG_SIZE];

/* Reproduce EXACTAMENTE el encuadre del proxy (app/tools/framing.py): cuadrado
 * centrado. Si las dos se separan, lo que encuadras deja de ser lo que recibes. */
static void framing_init(int src_w, int src_h)
{
    const float escala = fmaxf((float)LCD_IMG_SIZE / src_w, (float)LCD_IMG_SIZE / src_h);
    const float sobra_x = (src_w * escala - LCD_IMG_SIZE) / 2.0f;
    const float sobra_y = (src_h * escala - LCD_IMG_SIZE) / 2.0f;

    for (int i = 0; i < LCD_IMG_SIZE; i++) {
        int sx = (int)((i + sobra_x) / escala);
        int sy = (int)((i + sobra_y) / escala);
        s_lut_col[i] = (uint16_t)(sx < 0 ? 0 : (sx >= src_w ? src_w - 1 : sx));
        s_lut_fila[i] = (uint16_t)(sy < 0 ? 0 : (sy >= src_h ? src_h - 1 : sy));
    }
    ESP_LOGI(TAG, "encuadre: sensor %dx%d -> cuadro %dx%d, se ve el %.0f%% del ancho",
             src_w, src_h, LCD_IMG_SIZE, LCD_IMG_SIZE,
             LCD_IMG_SIZE / (src_w * escala) * 100.0f);
}

static void copiar_frame(const camera_fb_t *fb)
{
    const uint16_t *src = (const uint16_t *)fb->buf;
    for (int y = 0; y < LCD_IMG_SIZE; y++) {
        const uint16_t *fila = &src[(size_t)s_lut_fila[y] * fb->width];
        uint16_t *dst = &s_preview[y * LCD_IMG_SIZE];
        for (int x = 0; x < LCD_IMG_SIZE; x++) {
            dst[x] = fila[s_lut_col[x]];
        }
    }
}

/* Mensajes que el usuario pueda entender, en vez de un codigo de ESP-IDF. */
static const char *explicar(esp_err_t err)
{
    switch (err) {
        case ESP_ERR_HTTP_CONNECT: return "No se pudo contactar\ncon el servidor.\nRevisa el WiFi.";
        case ESP_ERR_NO_MEM:       return "La imagen no cabe\nen memoria.";
        case ESP_ERR_TIMEOUT:      return "El servidor tardo\ndemasiado.";
        default:                   return "Fallo al estilizar.\nInientalo otra vez.";
    }
}

static void estado_listo(void)
{
    UI_EstadoWifi(true);
    UI_EstadoServidor(true);
}

/* El cliente HTTP rellena directamente s_resultado, asi que aqui no hay nada
 * que copiar: solo avisar a la interfaz. El primer borrador llega sobre los 7 s
 * y el definitivo sobre los 20, asi que en vez de mirar una barra en blanco se
 * ve la foto dibujandose y afinandose. */
static void on_frame(const uint8_t *frame, size_t len, int indice, void *user)
{
    if (indice == 0) {
        UI_MostrarResultado();   /* quita la capa de espera y saca el borrador */
    } else {
        UI_RefrescarResultado();
    }
    ESP_LOGI(TAG, "frame %d en pantalla", indice);
}

static void capturar_y_estilizar(void)
{
    const char *estilo = MODO_DIAGNOSTICO ? "test" : UI_EstiloActual();
    const int64_t t0 = esp_timer_get_time();
    s_capturando = true;

    /* El frame que el usuario tenia delante al pulsar: es "el original". */
    memcpy(s_original, s_preview, IMG_BYTES);

    UI_MostrarOcupado("Capturando");
    if (Camera_SetMode(CAMERA_MODE_PHOTO) != ESP_OK) {
        UI_MostrarError("La camara no responde.");
        return;
    }
    for (int i = 0; i < 2; i++) {   /* los primeros frames salen mal expuestos */
        camera_fb_t *descarte = esp_camera_fb_get();
        if (descarte != NULL) {
            esp_camera_fb_return(descarte);
        }
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL || fb->len > JPEG_MAX) {
        if (fb != NULL) {
            esp_camera_fb_return(fb);
        }
        ESP_LOGE(TAG, "no se pudo tomar la foto");
        UI_MostrarError("No se pudo tomar\nla foto.");
        Camera_SetMode(CAMERA_MODE_PREVIEW);
        return;
    }

    const size_t jpeg_len = fb->len;
    ESP_LOGI(TAG, "foto %dx%d, %u bytes, estilo \"%s\"",
             fb->width, fb->height, (unsigned)jpeg_len, estilo);

    /* Copiar y apagar la camara ANTES de subir: mientras su DMA sigue viva, el
     * WiFi no llega a tiempo a los ACK y lwIP aborta la conexion. */
    memcpy(s_jpeg, fb->buf, jpeg_len);
    esp_camera_fb_return(fb);
    Camera_Stop();
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Mismo id en todos los intentos: el servidor devuelve el resultado ya
     * calculado en vez de volver a pagar otra generacion. */
    char capture_id[24];
    snprintf(capture_id, sizeof(capture_id), "%08" PRIx32 "%08" PRIx32,
             esp_random(), (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF));

    size_t recibidos = 0;
    esp_err_t err = ESP_FAIL;
    for (int intento = 1; intento <= INTENTOS_MAX; intento++) {
        char aviso[48];
        if (intento == 1) {
            snprintf(aviso, sizeof(aviso), "Dibujando");
        } else {
            snprintf(aviso, sizeof(aviso), "Reintentando (%d/%d)", intento, INTENTOS_MAX);
        }
        UI_ActualizarOcupado(aviso);

        err = http_post_photo(CONFIG_ESP_CAM_SERVER_URL, estilo, capture_id,
                              s_jpeg, jpeg_len, s_resultado, IMG_BYTES, &recibidos,
                              CONFIG_ESP_CAM_HTTP_TIMEOUT_MS, on_frame, NULL);
        if (err == ESP_OK && recibidos == IMG_BYTES) {
            break;
        }
        if (err == ESP_OK) {
            ESP_LOGE(TAG, "llegaron %u bytes y hacen falta %d", (unsigned)recibidos, IMG_BYTES);
            err = ESP_ERR_INVALID_SIZE;
        }
        ESP_LOGW(TAG, "intento %d/%d fallido (%s)", intento, INTENTOS_MAX, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (err == ESP_OK) {
        const int64_t duracion_ms = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "\"%s\" listo en %" PRId64 " s", estilo, duracion_ms / 1000);
        /* La proxima barra se reparte sobre lo que acaba de costar esta foto. */
        UI_CalibrarEspera((uint32_t)duracion_ms);
        UI_MostrarResultado();
        /* Acabamos de hablar con el servidor: no hay duda de que responde. */
        estado_listo();
    } else {
        UI_MostrarError(explicar(err));
    }

    Camera_SetMode(CAMERA_MODE_PREVIEW);
    s_capturando = false;
}

/* La comprobacion de salud va en su propia tarea por dos razones: bloquea
 * varios segundos y congelaria el visor, y una sola comprobacion al arrancar
 * se queda pegada en "no responde" si justo esa cae en un bache de la red. */
static void tarea_salud(void *arg)
{
    char cuerpo[128];

    while (1) {
        const bool con_wifi = wifi_is_connected();
        UI_EstadoWifi(con_wifi);
        if (!s_capturando && con_wifi) {
            const bool ok = http_health_check(CONFIG_ESP_CAM_SERVER_URL, 8000,
                                              cuerpo, sizeof(cuerpo)) == ESP_OK;
            UI_EstadoServidor(ok);
        }
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}

static void conectar_red(void)
{
    UI_EstadoWifi(false);
    UI_EstadoServidor(false);
    const esp_err_t err = wifi_connect_sta(CONFIG_ESP_CAM_WIFI_SSID,
                                            CONFIG_ESP_CAM_WIFI_PASSWORD, 20000, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sin WiFi: %s", esp_err_to_name(err));
        return;
    }
    /* La IP ya no cabe en pantalla ahora que el estado son dos iconos, pero
     * sigue haciendo falta para apuntarle el navegador o un ping. */
    ESP_LOGI(TAG, "IP de la camara: %s", wifi_get_ip_str());
    UI_EstadoWifi(true);
}

void app_main(void)
{
    ESP_ERROR_CHECK(http_client_init());
    ESP_ERROR_CHECK(LCD_Init());
    ESP_ERROR_CHECK(LCD_SetBrightness(100));
    ESP_ERROR_CHECK(Touch_Init());
    ESP_ERROR_CHECK(LVGL_Init());

    s_preview = heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM);
    s_original = heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM);
    s_resultado = heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM);
    s_jpeg = heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK((s_preview && s_original && s_resultado && s_jpeg) ? ESP_OK : ESP_ERR_NO_MEM);
    memset(s_preview, 0, IMG_BYTES);

    UI_Init((uint8_t *)s_preview, (uint8_t *)s_original, s_resultado);

    ESP_ERROR_CHECK(Camera_Init());
    framing_init(CAMERA_PREVIEW_W, CAMERA_PREVIEW_H);

    conectar_red();
    xTaskCreate(tarea_salud, "salud", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "listo. Interna %u KB | PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    int64_t t_log = esp_timer_get_time();
    uint32_t frames = 0;

    while (1) {
        if (UI_TomarPeticionCaptura()) {
            capturar_y_estilizar();
            continue;
        }
        if (UI_TomarPeticionVolver()) {
            UI_MostrarPreview();
            continue;
        }

        if (Camera_IsRunning()) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb != NULL) {
                copiar_frame(fb);
                esp_camera_fb_return(fb);
                UI_RefrescarPreview();
                frames++;
            }
        }

        const int64_t ahora = esp_timer_get_time();
        if (ahora - t_log >= 5000000) {
            ESP_LOGI(TAG, "%.1f fps visor | interna %u KB | PSRAM %u KB",
                     frames * 1000000.0f / (ahora - t_log),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            frames = 0;
            t_log = ahora;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
