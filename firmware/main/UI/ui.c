#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "LCD_Driver.h"
#include "LVGL_Driver.h"
#include "esp_log.h"

static const char *TAG = "UI";

#define BARRA_ALTO    (LCD_V_RES - LCD_IMG_SIZE)   /* 80 px */
#define DISPARO_DIAM  62
#define RUEDA_ANCHO   108

/* Nombre corto para la pantalla y nombre que entiende el servidor. Sin tildes:
 * las fuentes Montserrat que trae LVGL no cubren los acentos. */
static const struct {
    const char *etiqueta;
    const char *api;
} ESTILOS[] = {
    {"Caricatura", "cartoon"},
    {"Anime",      "anime"},
    {"Lapiz",      "sketch"},
    {"Acuarela",   "watercolor"},
    {"Pixel art",  "pixel"},
};
#define N_ESTILOS (sizeof(ESTILOS) / sizeof(ESTILOS[0]))

static lv_obj_t *s_img;
static lv_obj_t *s_rueda;                       /* selector de estilo, solo en el visor */
static lv_obj_t *s_btn_orig, *s_lbl_orig;       /* alternar original/IA, solo en resultado */
static lv_obj_t *s_btn_disparo, *s_lbl_disparo; /* accion principal, siempre visible */
static lv_obj_t *s_ico_wifi, *s_ico_servidor;
static lv_obj_t *s_capa_ocupado, *s_lbl_ocupado, *s_barra_ocupado;
static lv_timer_t *s_timer_ocupado;
static void parar_timer_ocupado(void);
static char s_texto_ocupado[64];
static uint32_t s_segundos_ocupado;
static uint32_t s_inicio_ocupado;

/* Cuanto se supone que va a tardar la proxima foto. El primer disparo no tiene
 * con que calibrarse, asi que arranca con una estimacion; a partir de ahi manda
 * lo que tardo la anterior, que ya incorpora la red y la calidad reales. */
#define ESPERA_INICIAL_MS 30000
#define BARRA_MAX         1000
static uint32_t s_espera_ms = ESPERA_INICIAL_MS;

static lv_img_dsc_t s_dsc;
static uint8_t *s_buf_preview, *s_buf_original, *s_buf_resultado;

static uint8_t s_estilo;
static bool s_pide_captura, s_pide_volver;
static bool s_viendo_original;
static enum { VISTA_PREVIEW, VISTA_RESULTADO, VISTA_ERROR } s_vista;

const char *UI_EstiloActual(void)
{
    return ESTILOS[s_estilo].api;
}

bool UI_TomarPeticionCaptura(void)
{
    const bool p = s_pide_captura;
    s_pide_captura = false;
    return p;
}

bool UI_TomarPeticionVolver(void)
{
    const bool p = s_pide_volver;
    s_pide_volver = false;
    return p;
}

static void apuntar_a(uint8_t *buf)
{
    s_dsc.data = buf;
    lv_img_set_src(s_img, &s_dsc);
    lv_obj_invalidate(s_img);
}

static void mostrar(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --- eventos --- */

static void on_rueda(lv_event_t *e)
{
    /* En modo infinito la rueda repite la lista varias veces por dentro, pero
     * lv_roller_get_selected ya devuelve el indice real. */
    s_estilo = (uint8_t)lv_roller_get_selected(lv_event_get_target(e));
    ESP_LOGI(TAG, "estilo: %s", ESTILOS[s_estilo].api);
}

static void on_boton_orig(lv_event_t *e)
{
    if (s_vista != VISTA_RESULTADO) {
        return;
    }
    s_viendo_original = !s_viendo_original;
    apuntar_a(s_viendo_original ? s_buf_original : s_buf_resultado);
    lv_label_set_text(s_lbl_orig, s_viendo_original ? "Ver IA" : "Original");
}

static void on_boton_disparo(lv_event_t *e)
{
    if (s_vista == VISTA_PREVIEW) {
        s_pide_captura = true;
    } else {
        s_pide_volver = true;
    }
}

/* --- construccion --- */

/* Disparador redondo y translucido, al estilo del obturador de una camara: se
 * apoya sobre la barra oscura sin taparla del todo y se aclara al pulsarlo. */
static void crear_disparador(lv_obj_t *scr)
{
    s_btn_disparo = lv_btn_create(scr);
    lv_obj_set_size(s_btn_disparo, DISPARO_DIAM, DISPARO_DIAM);
    lv_obj_align(s_btn_disparo, LV_ALIGN_BOTTOM_RIGHT, -16, -(BARRA_ALTO - DISPARO_DIAM) / 2);
    lv_obj_add_event_cb(s_btn_disparo, on_boton_disparo, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_radius(s_btn_disparo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_disparo, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_btn_disparo, LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_btn_disparo, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_btn_disparo, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_btn_disparo, 3, 0);
    lv_obj_set_style_shadow_width(s_btn_disparo, 0, 0);
    lv_obj_set_style_bg_opa(s_btn_disparo, LV_OPA_80, LV_STATE_PRESSED);

    s_lbl_disparo = lv_label_create(s_btn_disparo);
    lv_obj_set_style_text_color(s_lbl_disparo, lv_color_white(), 0);
    lv_obj_center(s_lbl_disparo);
}

static void crear_rueda(lv_obj_t *scr)
{
    /* Una sola fuente de verdad: las opciones salen de ESTILOS, no de una
     * cadena repetida a mano que se quedaria descolgada al anadir un estilo. */
    char opciones[160];
    int n = 0;
    for (size_t i = 0; i < N_ESTILOS && n < (int)sizeof(opciones); i++) {
        n += snprintf(opciones + n, sizeof(opciones) - n, "%s%s",
                      i ? "\n" : "", ESTILOS[i].etiqueta);
    }

    s_rueda = lv_roller_create(scr);
    lv_roller_set_options(s_rueda, opciones, LV_ROLLER_MODE_INFINITE);

    /* El interlineado va antes del recuento de filas: LVGL calcula el alto a
     * partir de el, y con el espaciado por defecto tres filas miden 100 px y se
     * salen de la barra, tapando el borde de la foto. */
    lv_obj_set_style_text_line_space(s_rueda, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_rueda, 2, LV_PART_MAIN);
    lv_roller_set_visible_row_count(s_rueda, 3);
    lv_obj_set_width(s_rueda, RUEDA_ANCHO);
    lv_obj_align(s_rueda, LV_ALIGN_BOTTOM_LEFT, 12, -9);
    lv_obj_add_event_cb(s_rueda, on_rueda, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_style_bg_color(s_rueda, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_text_color(s_rueda, lv_color_hex(0x8b949e), 0);
    lv_obj_set_style_border_width(s_rueda, 0, 0);
    lv_obj_set_style_radius(s_rueda, 8, 0);
    lv_obj_set_style_bg_color(s_rueda, lv_color_hex(0x1f6feb), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_rueda, lv_color_white(), LV_PART_SELECTED);
}

/* Los dos indicadores van superpuestos sobre la esquina de la imagen, como el
 * HUD de una camara: se leen de un vistazo y no gastan alto de la barra. */
static void crear_indicadores(lv_obj_t *scr)
{
    lv_obj_t *hud = lv_obj_create(scr);
    lv_obj_set_size(hud, 62, 28);
    lv_obj_align(hud, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(hud, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(hud, LV_OPA_60, 0);
    lv_obj_set_style_border_width(hud, 0, 0);
    lv_obj_set_style_radius(hud, 14, 0);
    lv_obj_set_style_pad_all(hud, 0, 0);
    lv_obj_clear_flag(hud, LV_OBJ_FLAG_SCROLLABLE);

    s_ico_wifi = lv_label_create(hud);
    lv_label_set_text(s_ico_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_ico_wifi, LV_ALIGN_LEFT_MID, 9, 0);

    s_ico_servidor = lv_label_create(hud);
    lv_label_set_text(s_ico_servidor, LV_SYMBOL_UPLOAD);
    lv_obj_align(s_ico_servidor, LV_ALIGN_RIGHT_MID, -9, 0);
}

void UI_Init(uint8_t *buffer_preview, uint8_t *buffer_original, uint8_t *buffer_resultado)
{
    s_buf_preview = buffer_preview;
    s_buf_original = buffer_original;
    s_buf_resultado = buffer_resultado;

    LVGL_Lock(-1);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_dsc.header.always_zero = 0;
    s_dsc.header.w = LCD_IMG_SIZE;
    s_dsc.header.h = LCD_IMG_SIZE;
    s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_dsc.data_size = (uint32_t)LCD_IMG_SIZE * LCD_IMG_SIZE * 2;
    s_dsc.data = s_buf_preview;

    s_img = lv_img_create(scr);
    lv_img_set_src(s_img, &s_dsc);
    lv_obj_align(s_img, LV_ALIGN_TOP_MID, 0, 0);

    crear_indicadores(scr);
    crear_rueda(scr);
    crear_disparador(scr);
    lv_label_set_text(s_lbl_disparo, LV_SYMBOL_IMAGE);

    s_btn_orig = lv_btn_create(scr);
    lv_obj_set_size(s_btn_orig, RUEDA_ANCHO, 40);
    lv_obj_align(s_btn_orig, LV_ALIGN_BOTTOM_LEFT, 12, -20);
    lv_obj_add_event_cb(s_btn_orig, on_boton_orig, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_orig, LV_OBJ_FLAG_HIDDEN);
    s_lbl_orig = lv_label_create(s_btn_orig);
    lv_label_set_text(s_lbl_orig, "Original");
    lv_obj_center(s_lbl_orig);

    /* Capa de espera: tapa el cuadro de imagen mientras trabaja la IA. */
    s_capa_ocupado = lv_obj_create(scr);
    lv_obj_set_size(s_capa_ocupado, LCD_IMG_SIZE, LCD_IMG_SIZE);
    lv_obj_align(s_capa_ocupado, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_capa_ocupado, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(s_capa_ocupado, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_capa_ocupado, 0, 0);
    lv_obj_clear_flag(s_capa_ocupado, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_capa_ocupado, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *spinner = lv_spinner_create(s_capa_ocupado, 1200, 60);
    lv_obj_set_size(spinner, 72, 72);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    s_barra_ocupado = lv_bar_create(s_capa_ocupado);
    lv_obj_set_size(s_barra_ocupado, LCD_IMG_SIZE - 72, 8);
    lv_obj_align(s_barra_ocupado, LV_ALIGN_CENTER, 0, 26);
    lv_bar_set_range(s_barra_ocupado, 0, BARRA_MAX);
    lv_bar_set_value(s_barra_ocupado, 0, LV_ANIM_OFF);

    s_lbl_ocupado = lv_label_create(s_capa_ocupado);
    lv_label_set_long_mode(s_lbl_ocupado, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_ocupado, LCD_IMG_SIZE - 24);
    lv_obj_set_style_text_align(s_lbl_ocupado, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_ocupado, LV_ALIGN_CENTER, 0, 52);
    lv_label_set_text(s_lbl_ocupado, "");

    s_vista = VISTA_PREVIEW;

    /* La altura de la rueda la decide LVGL a partir de la fuente, no nosotros:
     * merece la pena dejar constancia de que cae dentro de la barra y no se
     * come el cuadro de imagen. */
    lv_obj_update_layout(scr);
    ESP_LOGI(TAG, "rueda %dx%d en y=%d | disparador %dx%d en y=%d | barra desde y=%d",
             (int)lv_obj_get_width(s_rueda), (int)lv_obj_get_height(s_rueda),
             (int)lv_obj_get_y(s_rueda),
             (int)lv_obj_get_width(s_btn_disparo), (int)lv_obj_get_height(s_btn_disparo),
             (int)lv_obj_get_y(s_btn_disparo), LCD_IMG_SIZE);
    LVGL_Unlock();
    ESP_LOGI(TAG, "interfaz lista: cuadro %dx%d, barra de %d px",
             LCD_IMG_SIZE, LCD_IMG_SIZE, BARRA_ALTO);
}

/* --- cambios de vista --- */

void UI_RefrescarPreview(void)
{
    if (s_vista != VISTA_PREVIEW) {
        return;   /* no gastar repintados en algo que no se ve */
    }
    LVGL_Lock(-1);
    lv_obj_invalidate(s_img);
    LVGL_Unlock();
}

void UI_MostrarPreview(void)
{
    LVGL_Lock(-1);
    s_vista = VISTA_PREVIEW;
    s_viendo_original = false;
    apuntar_a(s_buf_preview);
    parar_timer_ocupado();
    lv_obj_add_flag(s_capa_ocupado, LV_OBJ_FLAG_HIDDEN);
    mostrar(s_rueda, true);
    mostrar(s_btn_orig, false);
    lv_label_set_text(s_lbl_disparo, LV_SYMBOL_IMAGE);
    LVGL_Unlock();
}

/* Progreso 0..BARRA_MAX a partir del tiempo transcurrido.
 *
 * Lineal solo hasta el 90 %: pasada la estimacion la barra sigue avanzando pero
 * cada vez menos, acercandose a BARRA_MAX sin llegar nunca. Una barra que se
 * planta en el 100 % y ahi se queda parece un cuelgue, y el tiempo real se va
 * facilmente de la estimacion (reintentos, red floja, un estilo mas caro). Asi
 * nunca promete un final que todavia no tiene. */
static uint32_t progreso_ocupado(uint32_t transcurrido)
{
    const uint32_t estimado = s_espera_ms;
    if (transcurrido < estimado) {
        return (uint32_t)((uint64_t)transcurrido * 900 / estimado);
    }
    const uint32_t extra = transcurrido - estimado;
    return 900 + (uint32_t)((uint64_t)99 * extra / (extra + estimado));
}

static void pintar_ocupado(void)
{
    lv_label_set_text_fmt(s_lbl_ocupado, "%s\n%" LV_PRIu32 " s",
                          s_texto_ocupado, s_segundos_ocupado);
}

/* La llamada de red bloquea la tarea de la aplicacion, asi que el avance lo
 * lleva LVGL por su cuenta: es la unica senal de que sigue vivo durante los
 * 20-40 s que tarda el modelo. Va a 100 ms para que la barra se mueva suave, y
 * el texto solo se repinta cuando cambia el segundo. */
static void tick_ocupado(lv_timer_t *t)
{
    const uint32_t transcurrido = lv_tick_elaps(s_inicio_ocupado);
    lv_bar_set_value(s_barra_ocupado, (int32_t)progreso_ocupado(transcurrido), LV_ANIM_OFF);

    const uint32_t segundos = transcurrido / 1000;
    if (segundos != s_segundos_ocupado) {
        s_segundos_ocupado = segundos;
        pintar_ocupado();
    }
}

void UI_CalibrarEspera(uint32_t milisegundos)
{
    /* Solo se llama tras una foto que salio bien: un fallo mide timeouts, no lo
     * que tarda el modelo, y calibrar con eso estropearia la siguiente barra. */
    if (milisegundos < 1000) {
        return;
    }
    s_espera_ms = milisegundos;
    ESP_LOGI(TAG, "barra de espera calibrada a %" PRIu32 " ms", s_espera_ms);
}

static void parar_timer_ocupado(void)
{
    if (s_timer_ocupado != NULL) {
        lv_timer_del(s_timer_ocupado);
        s_timer_ocupado = NULL;
    }
}

void UI_MostrarOcupado(const char *texto)
{
    LVGL_Lock(-1);
    strlcpy(s_texto_ocupado, texto, sizeof(s_texto_ocupado));
    s_segundos_ocupado = 0;
    s_inicio_ocupado = lv_tick_get();
    lv_obj_clear_flag(s_capa_ocupado, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_capa_ocupado);
    lv_obj_clear_flag(s_barra_ocupado, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_barra_ocupado, 0, LV_ANIM_OFF);
    pintar_ocupado();
    if (s_timer_ocupado == NULL) {
        s_timer_ocupado = lv_timer_create(tick_ocupado, 100, NULL);
    }
    LVGL_Unlock();
}

void UI_ActualizarOcupado(const char *texto)
{
    LVGL_Lock(-1);
    strlcpy(s_texto_ocupado, texto, sizeof(s_texto_ocupado));
    pintar_ocupado();
    LVGL_Unlock();
}

void UI_MostrarResultado(void)
{
    LVGL_Lock(-1);
    s_vista = VISTA_RESULTADO;
    s_viendo_original = false;
    parar_timer_ocupado();
    lv_obj_add_flag(s_capa_ocupado, LV_OBJ_FLAG_HIDDEN);
    apuntar_a(s_buf_resultado);
    mostrar(s_rueda, false);
    mostrar(s_btn_orig, true);
    lv_label_set_text(s_lbl_orig, "Original");
    lv_label_set_text(s_lbl_disparo, LV_SYMBOL_LEFT);
    LVGL_Unlock();
}

void UI_RefrescarResultado(void)
{
    if (s_vista != VISTA_RESULTADO || s_viendo_original) {
        return;
    }
    LVGL_Lock(-1);
    lv_obj_invalidate(s_img);
    LVGL_Unlock();
}

void UI_MostrarError(const char *texto)
{
    LVGL_Lock(-1);
    s_vista = VISTA_ERROR;
    parar_timer_ocupado();
    lv_obj_clear_flag(s_capa_ocupado, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_capa_ocupado);
    lv_obj_add_flag(s_barra_ocupado, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_ocupado, texto);
    mostrar(s_rueda, false);
    mostrar(s_btn_orig, false);
    lv_label_set_text(s_lbl_disparo, LV_SYMBOL_LEFT);
    LVGL_Unlock();
}

/* --- indicadores --- */

static void pintar_icono(lv_obj_t *ico, bool ok)
{
    lv_obj_set_style_text_color(ico, ok ? lv_palette_main(LV_PALETTE_GREEN)
                                        : lv_palette_main(LV_PALETTE_RED), 0);
}

void UI_EstadoWifi(bool ok)
{
    LVGL_Lock(-1);
    pintar_icono(s_ico_wifi, ok);
    LVGL_Unlock();
}

void UI_EstadoServidor(bool ok)
{
    LVGL_Lock(-1);
    pintar_icono(s_ico_servidor, ok);
    LVGL_Unlock();
}
