"""Proxy de estilización para ESP_CAM.

Recibe un JPEG del ESP32-S3, lo estiliza con OpenAI y devuelve la imagen ya
reescalada a 240x320 en RGB565 crudo, lista para volcar a la LCD.
"""

import logging

from fastapi import FastAPI

from app.api.routes import router

# Sin esto los log.info de la app no salen por ningun lado: uvicorn solo
# engancha sus propios loggers, y la traza de frames y de tokens gastados se
# perdia entera.
logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s: %(message)s")

app = FastAPI(title="ESP_CAM stylize proxy")
app.include_router(router)
