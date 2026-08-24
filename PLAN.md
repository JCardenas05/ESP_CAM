# ESP_CAM — Cámara de bolsillo con post-proceso por IA

Placa: **Waveshare ESP32-S3-Touch-LCD-2** + cámara **TY-OV5640** (DVP 8 bits).
Objetivo: tomar foto → enviarla a un modelo de generación/edición de imagen → mostrar en la
LCD el resultado estilizado (anime, caricatura, retrato dibujado, etc.).

---

## 0. Hechos verificados del hardware y del entorno

Comprobado por USB el 2026-08-22 (`esptool flash_id` sobre `/dev/ttyACM0`):

| Dato | Valor |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2 |
| PSRAM | 8 MB embebida, **octal**, 80 MHz |
| Flash | 16 MB (quad, 3.3 V) |
| MAC | 28:84:85:47:b9:9c |
| Puerto | `/dev/ttyACM0` (USB-Serial/JTAG nativo, VID:PID `303a:1001`) |
| ESP-IDF | v5.3.1 en `~/esp/v5.3.1/esp-idf` |

**Problema de entorno detectado y resuelto:** el venv `idf5.3_py3.13_env` quedó inservible
(Fedora actualizó el sistema a Python 3.14 y `export.sh` intentaba usar un
`idf5.3_py3.14_env` inexistente). El venv `idf5.3_py3.12_env` sí está completo. Por eso
existe `./idf.sh`, que fuerza ese venv. **Siempre arrancar la sesión con:**

```bash
cd ~/Documentos/ESP_CAM && source ./idf.sh
```

### Pinout real (extraído del demo oficial `reference/waveshare-demos/05_lvgl_camera`)

**Cámara DVP (OV5640):**

| Señal | GPIO | | Señal | GPIO |
|---|---|---|---|---|
| PWDN  | 17 | | Y9 (D7) | 2 |
| RESET | -1 (reset por software) | | Y8 (D6) | 7 |
| XCLK  | 8  | | Y7 (D5) | 10 |
| SIOD (SDA) | 21 | | Y6 (D4) | 14 |
| SIOC (SCL) | 16 | | Y5 (D3) | 11 |
| VSYNC | 6  | | Y4 (D2) | 15 |
| HREF  | 4  | | Y3 (D1) | 13 |
| PCLK  | 9  | | Y2 (D0) | 12 |

**LCD ST7789T3 (SPI, 240×320):** SCLK 39, MOSI 38, MISO 40, CS 45, DC 42, RST -1,
backlight 1 (PWM por LEDC), reloj de píxel 80 MHz.

**Touch CST816S (I2C0):** SDA 48, SCL 47. **IMU QMI8658:** mismo bus I2C.

No hay colisión de GPIO entre cámara, LCD y touch: las tres se pueden tener vivas a la vez.

### Decisión de arquitectura (importante)

Recomendación: **no llamar a OpenAI directamente desde el ESP32 en las primeras etapas.**
Un proxy propio (Python/FastAPI en la PC o en un VPS) hace el trabajo pesado:

- El ESP32 sube un JPEG de ~40 KB y recibe de vuelta **RGB565 crudo de 240×320 = 153 600 bytes**,
  que se vuelca a la LCD sin decodificar nada.
- Si el ESP32 llamara a la API directamente tendría que: montar multipart/form-data, hacer TLS
  contra `api.openai.com`, y recibir un PNG de 1024×1024 en **base64** (~2 MB) que después
  habría que decodificar y reescalar en el dispositivo. Es posible con 8 MB de PSRAM, pero es
  mucho trabajo extra y complica cada etapa de depuración.
- Bonus del proxy: la API key **no** vive en el firmware, se pueden cambiar prompts y estilos sin
  reflashear, y se cachea/registra cada resultado.

La ruta directa queda como Etapa 9 opcional, una vez que todo lo demás funcione.

---

## Estructura del repo

Sigue las convenciones de [Pomodoro_ESP](https://github.com/JCardenas05/Pomodoro_ESP):
drivers y lógica de la app en subcarpetas de `main/`, componentes reutilizables en
`components/`, y el backend FastAPI en capas `api / core / models / services / tools`.

```
ESP_CAM/
├── idf.sh                  # source ./idf.sh  → carga ESP-IDF con el venv correcto
├── PLAN.md                 # este documento
├── firmware/
│   ├── main/
│   │   ├── main.c
│   │   ├── idf_component.yml    # lvgl ~8.4, esp_lcd_touch_cst816s
│   │   ├── Board/board_pins.h   # UNICA fuente de verdad de los pines
│   │   ├── LCD_Driver/          # ST7789T3 + retroiluminacion PWM
│   │   ├── Touch_Driver/        # CST816S sobre i2c_master
│   │   └── LVGL_Driver/         # port de LVGL 8.4 (flush, tick, mutex)
│   ├── components/
│   │   ├── Wifi_Connect/        # STA con reintentos y espera de IP
│   │   └── HTTPClient/          # /health ahora, POST de la foto en la Etapa 7
│   ├── main/Kconfig.projbuild   # SSID, clave, URL del proxy (van a sdkconfig, ignorado)
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── server/
│   ├── app/
│   │   ├── main.py              # crea la app e incluye el router
│   │   ├── api/routes.py        # /health y /stylize
│   │   ├── core/config.py       # Config con clases anidadas + validate()
│   │   ├── models/stylize.py    # enum de estilos y sus prompts
│   │   ├── services/openai_image.py  # llamada a gpt-image-1
│   │   ├── tools/framing.py     # recorte cuadrado, igual que el visor
│   │   ├── tools/rgb565.py      # PNG → RGB565 big-endian
│   ├── tools_preview.py         # .raw → .png, para mirar lo que verá la LCD
│   ├── requirements.txt
│   ├── .env.example             # copiar a .env con la OPENAI_API_KEY
│   └── data/{originales,resultados}/
├── tools/
│   ├── capture_serial.py    # log serie no interactivo (idf.py monitor exige TTY)
│   └── grab_photo.py        # extrae por serie las fotos en base64 y las guarda .jpg
└── reference/waveshare-demos/   # demos ESP-IDF oficiales (fuente del pinout)
```

---

## Etapas

Cada etapa tiene un **criterio de validación** explícito. No se pasa a la siguiente sin cumplirlo.

### Etapa 1 — Proyecto en blanco que arranca
- `idf.py create-project firmware` dentro de `~/Documentos/ESP_CAM`.
- `idf.py set-target esp32s3`.
- `sdkconfig.defaults` con: flash 16 MB, PSRAM octal @80 MHz, CPU 240 MHz, tabla de
  particiones personalizada (app grande + `storage`), consola por USB-Serial/JTAG.
- `app_main` que imprima heap interno y heap PSRAM cada segundo.

**Validación:** `idf.py -p /dev/ttyACM0 flash monitor` muestra el log, reporta ~8 MB de PSRAM
libre y la placa no se reinicia en 60 s.

### Etapa 2 — LCD encendida
- Driver `esp_lcd_panel_st7789` sobre SPI2, backlight con LEDC.
- Test: barras de color, blanco/negro pleno y un rectángulo en cada esquina.

**Validación:** ✅ confirmado en pantalla. Parámetros correctos para este panel:
`invert_color(true)`, `rgb_ele_order = RGB`, `mirror(false,false)`, `swap_xy(false)`,
sin necesidad de `set_gap`. Los RGB565 se escriben **byte-swapped** (big-endian) —
por eso `bsp_rgb565()` intercambia los bytes.

### Etapa 3 — Touch + LVGL
- `esp_lcd_touch_cst816s` en I2C0 y LVGL 8.4 con doble buffer en PSRAM.
- UI mínima: un botón "Capturar" y una etiqueta con el contador de pulsaciones.

**Validación:** ✅ las 4 dianas se encendieron bajo el dedo, sin necesidad de tocar
`swap_xy` ni `mirror_*`. Nota: el CST816S necesita el driver **nuevo** `i2c_master`;
con el legacy `driver/i2c.h` del demo de Waveshare la placa entra en bucle de reinicio
porque la macro `ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG()` rellena `scl_speed_hz`.

### Etapa 4 — Cámara OV5640
- Componente `espressif/esp32-camera ^2.0.15` con el pinout de arriba.
- Dos modos: preview en **RGB565 QVGA** volcado directo a la LCD, y captura en **JPEG SVGA/UXGA**.

**Validación:** ✅ (b) confirmado: OV5640 detectado (PID 0x5640), foto de 800x600 y
31 106 bytes, SOI/EOI correctos, longitud declarada = recibida, imagen nítida y bien
expuesta al abrirla en el PC. Preview a ~8,9 fps (mejorable en la Etapa 8).

**Encuadre (decidido tras ver el preview):** cuadro de **240x240 centrado en y=40**, con
bandas negras de 40 px arriba y abajo. Muestra el 75 % del ancho del sensor. Es cuadrado a
propósito: los modelos de imagen generan cuadrado (1024x1024), así que el resultado de la IA
encaja sin recortar nada. El firmware (`framing_init()` en `main.c`) y el proxy
(`tools/rgb565.py::_fit_center`) tienen que aplicar **el mismo** encuadre; si se separan,
lo que encuadras deja de ser lo que recibes.

Dos detalles del OV5640 que costaron entender el "se ve con zoom": el sensor abre una
ventana distinta por relación de aspecto (4:3 → 2560x1920 completo; 3:2 → 2560x1704
recortado), y el preview además recortaba a lo bruto la mitad del ancho. Ahora preview y
foto usan ambos 4:3.

**Restricción descubierta aquí:** `esp32-camera` 2.1.7 solo usa el driver I2C nuevo con
ESP-IDF ≥ 5.4; en 5.3.1 compila `driver/sccb.c`, que usa el legacy. ESP-IDF **aborta en
un constructor, antes de `app_main`**, si los dos drivers I2C acaban en el mismo binario.
Por eso `Touch_Driver` volvió al driver legacy con `io_cfg.scl_speed_hz = 0`.

### Etapa 5 — WiFi y transporte
- WiFi STA con credenciales en NVS (no hardcodeadas), reconexión automática, indicador en la UI.
- Cliente `esp_http_client` haciendo GET a `/health` del proxy.

**Validación:** ✅ conecta a MI_RED en 1,3 s (IP 192.168.1.4) y `GET /health` devuelve 200
con el JSON correcto, tanto al arrancar como en las comprobaciones periódicas cada 15 s.

**Ojo con la RAM interna:** al levantar el WiFi cae de 271 KB a **116 KB** libres. La PSRAM
no se toca (7888 KB), así que en la Etapa 7 los buffers de subida y bajada tienen que ir a
PSRAM sí o sí.

El ESP32-S3 es **solo 2,4 GHz**: no vale conectarlo a una red de 5 GHz.

### Etapa 6 — Proxy de estilización (se desarrolla y prueba **sin** la placa)
- FastAPI: `POST /stylize` recibe multipart (`image=jpeg`, `style=anime|cartoon|sketch|...`).
- Llama a OpenAI (`/v1/images/edits` con `gpt-image-1`) usando el prompt asociado al estilo.
- Reescala el resultado a 240×320, lo convierte a **RGB565 big-endian crudo** y lo devuelve
  como `application/octet-stream` de 153 600 bytes exactos.
- Guarda original + resultado en disco para inspección.

**Validación:** ✅ `curl -F image=@foto.jpg -F style=cartoon .../stylize -o out.raw` devuelve
exactamente **115 200 bytes** con `x-image-side: 240`, y `tools_preview.py` lo reconstruye a
PNG: se ve el ventilador convertido en caricatura, con los colores correctos — lo que prueba
de paso que el RGB565 big-endian está bien generado.

**Latencia real: 36,5 s**, no los 10-25 s que estimé. Con `quality="low"` ya, así que el
spinner de la Etapa 8 necesita aguantar bien ese tiempo y el timeout del ESP32 (20 s ahora)
**hay que subirlo a 90 s** en la Etapa 7 o la petición morirá siempre.

Parámetros elegidos: `input_fidelity="high"` (sin eso el modelo se inventa otra cara) y
`quality="low"` (el resultado se ve a 240x240; pagar más resolución no se nota).

### Etapa 7 — Integración completa
- El firmware sube el JPEG por HTTP POST y recibe el RGB565 en streaming, escribiéndolo a la
  LCD por bloques (no hace falta bufferizar los 150 KB completos, aunque cabe en PSRAM).
- Máquina de estados: `PREVIEW → CAPTURANDO → SUBIENDO → ESPERANDO IA → MOSTRANDO`.

**Validación:** ✅ ciclo completo funcionando: foto → subida → IA → imagen en pantalla.
8/8 ciclos correctos en modo diagnóstico y 2/2 con la IA real. **Sin fugas**: 115 KB de RAM
interna en el ciclo 8, los mismos 116 KB del arranque.

**Lo que hizo falta para que la subida funcionara** (fallaba entera, atascándose tras los
primeros 4 KB):

| Cambio | Por qué |
|---|---|
| `esp_wifi_set_ps(WIFI_PS_NONE)` | con modem sleep los ACK llegan tarde y lwIP aborta |
| `LWIP_TCP_SND_BUF_DEFAULT` y `WND` a 65534 | los 5760 B por defecto se llenaban a los 4 KB |
| Trozos de subida de 1460 B (un MSS) | 4096 B obligaban a fragmentar dentro de lwIP |
| `vTaskDelay(300 ms)` tras `Camera_Stop()` | la pila de WiFi necesita recuperarse de la DMA |
| Hasta 3 reintentos del POST | abrir la conexión falla de forma intermitente |

**El cuello de botella real es la red, no el código.** Medido con ping a la placa:
`rtt min/avg/max/mdev = 9,7/203,8/868,5/183,9 ms` con 0 % de pérdida. 200 ms de media en una
LAN es muchísimo. La causa: `MI_RED` y `MI_RED_EXT` emiten **las dos en el canal 11**,
pisándose, y el PC también está por WiFi en esa misma banda. Arreglos por orden de eficacia:
PC por cable Ethernet; o separar los canales del router y el repetidor (1 y 6); o poner el PC
en la SSID de 5 GHz para liberar aire en 2,4 GHz.

**Ojo al coste:** cada reintento vuelve a llamar al modelo. En un ciclo que necesitó 3
intentos se pagaron 3 generaciones. La Etapa 8 debe darle al POST un identificador de captura
para que el servidor devuelva el resultado ya calculado en vez de rehacerlo.

**Herramientas de diagnóstico que quedan en el proyecto:** el endpoint `POST /echo` (mismo
camino de red, sin llamar a la IA ni gastar dinero) y el autotest de arranque del firmware,
que mide conexión, subida de 30 KB y bajada de 115 KB antes de tocar la cámara. Fue lo que
permitió separar "la red va mal" de "el proxy va mal". El firmware tiene además
`MODO_DIAGNOSTICO` en `main.c`: puesto a 1 manda las fotos a `/echo` y se puede iterar gratis.

### Etapa 8 — UX de "cámara de bolsillo"
- UI escrita **a mano con la API de LVGL** (no EEZ Studio): EEZ es una app de escritorio
  y no se puede pilotar desde aquí. Las pantallas van en `firmware/main/UI/`.
- Selector de estilo en pantalla (anime / caricatura / retrato a lápiz / acuarela / pixel art).
- Spinner con progreso y timeout, mensajes de error legibles (sin WiFi, proxy caído, API falló).
- Galería: guardar original + resultado en la tarjeta TF y poder navegarlos.
- Botón para alternar entre "foto real" y "foto IA" del mismo disparo.

**Validación:** un tercero puede usar el aparato sin instrucciones; los errores no cuelgan la UI.

### Etapa 9 — Endurecimiento (opcional)
- HTTPS real contra el proxy (certificado propio o bundle de CA).
- Alternativa: llamada directa a `api.openai.com` desde el dispositivo.
- Gestión de batería (ADC + icono), brillo adaptativo, deep sleep, OTA.

---

## Riesgos conocidos

| Riesgo | Mitigación |
|---|---|
| Corriente de arranque del OV5640 por USB | Alimentar con batería o hub con buena fuente si hay brownouts |
| Ancho de banda WiFi + DMA de cámara compiten | Parar el preview mientras se sube la foto |
| Latencia de la API (10-25 s) | Spinner + timeout de 60 s + reintento manual |
| Coste por imagen de `gpt-image-1` | Contador de peticiones en el proxy y límite diario |
| IDF 5.3.1 vs venv de Python | Resuelto con `./idf.sh` (ver Etapa 0) |
| IDF 5.3.1 obliga al I2C legacy | Todo el I2C por `driver/i2c.h`. Al subir a IDF ≥ 5.4 se puede migrar todo a `i2c_master` |

## Estado

- [x] Etapa 0 — entorno validado, pinout confirmado, demos de referencia en `reference/`
- [x] Etapa 1 — firmware arranca: PSRAM octal 8192 KB OK, CPU 240 MHz, flash qio 16 MB, sin reinicios
- [x] Etapa 2 — LCD OK: colores correctos (invert_color=true, orden RGB), marco completo
      sin gap, esquinas en su sitio (mirror off, swap_xy off), brillo PWM suave
- [x] Etapa 3 — tactil + LVGL 8.4.0 OK: 4/4 dianas responden bajo el dedo
      (swap_xy=0, mirror=0,0), ~45 flush/s, heap estable
- [x] Etapa 4 — OV5640 OK: preview 480x320 recortado a 240x320 (~8,9 fps) y foto JPEG
      800x600 validada abriendola en el PC; encuadre cuadrado 240x240 al 75 % del ancho
- [x] Etapa 5 — WiFi OK (IP 192.168.1.4) y `/health` responde 200; bandas de estado en la LCD
- [x] Etapa 6 — `/stylize` funciona de punta a punta: foto → OpenAI → 115 200 bytes RGB565
- [x] Etapa 7 — ciclo completo OK y sin fugas; la red de casa lo hace intermitente
      (reintentos lo salvan, ver notas de la etapa)
- [ ] Etapa 8 · [ ] 9
