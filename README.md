# ESP_CAM — a pocket camera with AI post-processing

A **Waveshare ESP32-S3-Touch-LCD-2** with a **TY-OV5640** camera: press the shutter,
the board takes the photo, sends it to a small proxy that restyles it with OpenAI's
image model, and the result appears on the LCD **painting itself top to bottom** as the
model generates it.

The first version of the image shows up about **9 seconds** after the shutter; the
final one lands around 22.

```
  ESP32-S3                          PC / server                    OpenAI
 ┌──────────┐   POST /stylize      ┌──────────────┐               ┌─────────┐
 │  OV5640  │  JPEG ~40 KB  ─────▶ │   FastAPI    │ ────────────▶ │  image  │
 │          │                      │              │  ◀──── draft  │   edit  │
 │  ST7789  │ ◀──── raw RGB565     │  square crop │  ◀──── draft  │(stream) │
 │ 240×320  │  115,200 B per frame │  + RGB565 BE │  ◀──── final  └─────────┘
 └──────────┘   (one per draft)    └──────────────┘
```

## Why a proxy instead of calling the API from the board

Because the ESP32 decodes **nothing**. The server hands it exactly
`240×240×2 = 115,200` bytes of big-endian RGB565, which is literally the format the
ST7789 panel wants to see, and the firmware dumps them straight to the display
untouched. If the board talked to OpenAI directly it would have to assemble
`multipart/form-data`, negotiate TLS, and decode a 1024×1024 PNG — with 116 KB of
internal RAM left once WiFi is up.

The proxy also absorbs the API key, which therefore never lives in the device's flash.

## Authentication

Every `POST /stylize` is a billed call to OpenAI, so once the proxy is reachable from
the internet it needs a door. Set `ESPCAM_TOKEN` in the server's `.env` and the same
value in the firmware's `menuconfig`; the board sends it as `Authorization: Bearer …`
and anything else gets a 401.

Leaving `ESPCAM_TOKEN` empty disables the check entirely — which is fine on a closed
LAN and reckless anywhere else. `GET /health` stays open either way: it carries no cost
and it is what keeps the reused TCP socket warm.

## What's in here

| Directory | Contents |
|---|---|
| `firmware/` | ESP-IDF v5.3.1 project: camera, LCD, touch and LVGL drivers, plus the UI |
| `server/` | FastAPI proxy: square crop, prompts, streaming, and RGB565 conversion |
| `tools/` | Host-side utilities to read the serial port and pull base64 photos out |
| `PLAN.md` | Development log by stage, with the real measurements (in Spanish) |

## Getting started

### Server

```bash
cd server
python3.12 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env        # then put the real OPENAI_API_KEY in it
./run.sh
```

The `--timeout-keep-alive 75` in `run.sh` is **not decorative**: the board reuses a
single TCP socket to skip the handshake on every photo, and what keeps that socket
alive is its own health check every 20 s. With uvicorn's default of 5 s, the server
closes the connection between checks and the reuse never happens at all.

### Firmware

```bash
source ./idf.sh             # loads ESP-IDF v5.3.1
cd firmware
idf.py menuconfig           # → "ESP_CAM Configuration"
idf.py flash monitor
```

`menuconfig` is where you set the WiFi SSID and password and the proxy URL (your PC's
LAN address, not `127.0.0.1`). They end up in `sdkconfig`, which is gitignored and so
**never reaches the repository**.

## The UI

Hand-written in C on **LVGL 8.4** — no interface generator involved. It has a
translucent circular shutter, a scroll-style style picker, WiFi and server icons that
go green or red, and a progress bar calibrated to 30 s on the first run and to the
previous photo's actual duration after that.

Five styles: `anime`, `cartoon`, `sketch`, `watercolor`, `pixel`.

## The prompts

The hard part wasn't applying a style — it was that the sensor is poor and the model
loves to "fix" people until they're unrecognisable. The prompt is split into blocks
(`server/app/models/stylize.py`):

- **Scene** — unconditional, and first: keep the framing, the number of people (zero
  included), and the subject matter. Without this block, a portrait-centric prompt
  makes the model **invent a face** when you hand it a photo of a wall.
- **Identity** — conditional on people being present: no slimming, beautifying,
  symmetrising or de-aging anyone.
- **Detail** — recover what the camera lost, but only detail the pixels genuinely
  imply.

Running with `input_fidelity="high"` and `quality="medium"`.

## Measured numbers

All of this is timed on the real device, not estimated:

| Phase | Time |
|---|---|
| Camera mode switch | 0.83 s |
| Capture | 0.30 s |
| Opening the connection (only the first one after boot) | 0.88 s |
| JPEG upload | 0.02 s |
| Model generation | ~20 s |
| Downloading one frame | 0.08 s |

Two optimisations that actually paid off:

- **Streaming the drafts** — the blank wait went from 25 s down to ~9 s. It costs 19 %
  more in output tokens (1056 → 1256), since every draft bills as a generated image.
- **Persistent connection** — the 880 ms handshake is paid once at boot instead of on
  every photo. The trick is *not* `keep_alive_enable` (that's TCP `SO_KEEPALIVE`, a
  false friend) but **not calling `esp_http_client_close()`**.

And one that didn't: going from `quality=low` to `medium` costs only +21 % in time
(16.8 s → 20.3 s) but nearly 4× in money.

## Hardware

| | |
|---|---|
| SoC | ESP32-S3R8 (QFN56) |
| PSRAM | 8 MB octal @ 80 MHz |
| Flash | 16 MB quad |
| LCD | ST7789T3 240×320 over SPI, 80 MHz pixel clock |
| Touch | CST816S (I2C) |
| Camera | OV5640 on an 8-bit DVP, 20 MHz XCLK |

The full pinout is in `PLAN.md`. Live preview is capped at ~7.9 fps by the sensor at
that XCLK.

## Still to do

- TF card gallery (SPI2 shared with the LCD, CS on 41).
- HTTPS, battery gauge, deep sleep, and OTA.

---

The official Waveshare demos that the pinout came from are **not included** here, being
third-party code; they're on the board's wiki.
