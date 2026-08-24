# Proxy de estilizacion — ESP_CAM

Recibe un JPEG del ESP32-S3, lo manda a OpenAI para estilizarlo y devuelve la imagen
ya reescalada a 240x320 en **RGB565 crudo** (153 600 bytes), lista para volcar a la LCD.

## Puesta en marcha

```bash
cd server
python3.12 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env   # y rellenar OPENAI_API_KEY
.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

`--host 0.0.0.0` es necesario para que el ESP32 lo alcance desde la LAN.

## Endpoints

| Metodo | Ruta | Estado |
|---|---|---|
| GET | `/health` | listo — objetivo de validacion de la Etapa 5 |
| POST | `/stylize` | pendiente — Etapa 6 |
