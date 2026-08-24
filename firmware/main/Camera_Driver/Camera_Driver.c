#include "Camera_Driver.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "Camera_Driver";

static camera_mode_t s_mode = CAMERA_MODE_PREVIEW;
static bool s_running;

camera_mode_t Camera_GetMode(void)
{
    return s_mode;
}

static camera_config_t config_para(camera_mode_t mode)
{
    camera_config_t cfg = {
        .pin_pwdn = BOARD_PIN_CAM_PWDN,
        .pin_reset = BOARD_PIN_CAM_RESET,
        .pin_xclk = BOARD_PIN_CAM_XCLK,
        .pin_sccb_sda = BOARD_PIN_CAM_SIOD,
        .pin_sccb_scl = BOARD_PIN_CAM_SIOC,
        .pin_d7 = BOARD_PIN_CAM_D7,
        .pin_d6 = BOARD_PIN_CAM_D6,
        .pin_d5 = BOARD_PIN_CAM_D5,
        .pin_d4 = BOARD_PIN_CAM_D4,
        .pin_d3 = BOARD_PIN_CAM_D3,
        .pin_d2 = BOARD_PIN_CAM_D2,
        .pin_d1 = BOARD_PIN_CAM_D1,
        .pin_d0 = BOARD_PIN_CAM_D0,
        .pin_vsync = BOARD_PIN_CAM_VSYNC,
        .pin_href = BOARD_PIN_CAM_HREF,
        .pin_pclk = BOARD_PIN_CAM_PCLK,

        .xclk_freq_hz = 20000000,
        /* El canal 0 de LEDC ya lo usa la retroiluminacion de la LCD: el XCLK
         * de la camara tiene que ir por otro, o uno pisa al otro. */
        .ledc_timer = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_1,

        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    if (mode == CAMERA_MODE_PREVIEW) {
        cfg.pixel_format = PIXFORMAT_RGB565;
        cfg.frame_size = FRAMESIZE_QVGA;   /* 320x240, 4:3 igual que la foto */
        cfg.fb_count = 2;
        cfg.jpeg_quality = 12;
    } else {
        cfg.pixel_format = PIXFORMAT_JPEG;
        cfg.frame_size = FRAMESIZE_SVGA;   /* 800x600 */
        cfg.fb_count = 1;
        cfg.jpeg_quality = 10;             /* menor numero = mejor calidad */
    }
    return cfg;
}

static esp_err_t arrancar(camera_mode_t mode)
{
    const camera_config_t cfg = config_para(mode);
    ESP_RETURN_ON_ERROR(esp_camera_init(&cfg), TAG, "esp_camera_init");

    sensor_t *s = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(s != NULL, ESP_FAIL, TAG, "sensor no detectado");

    /* Sin esto la imagen sale en espejo respecto a lo que ve el usuario. */
    s->set_hmirror(s, 1);

    s_mode = mode;
    s_running = true;
    ESP_LOGI(TAG, "modo %s: sensor PID 0x%04x, %s",
             mode == CAMERA_MODE_PREVIEW ? "PREVIEW (RGB565 320x240)" : "PHOTO (JPEG 800x600)",
             s->id.PID, s->id.PID == OV5640_PID ? "OV5640" : "sensor inesperado");
    return ESP_OK;
}

esp_err_t Camera_Init(void)
{
    return arrancar(CAMERA_MODE_PREVIEW);
}

esp_err_t Camera_SetMode(camera_mode_t mode)
{
    if (s_running && mode == s_mode) {
        return ESP_OK;
    }
    if (s_running) {
        ESP_RETURN_ON_ERROR(esp_camera_deinit(), TAG, "esp_camera_deinit");
        s_running = false;
    }
    return arrancar(mode);
}

esp_err_t Camera_Stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_camera_deinit(), TAG, "esp_camera_deinit");
    s_running = false;
    ESP_LOGI(TAG, "camara parada para dejar el aire libre al WiFi");
    return ESP_OK;
}

bool Camera_IsRunning(void)
{
    return s_running;
}
