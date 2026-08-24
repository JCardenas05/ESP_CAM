"""Llamada al modelo de generación de imagen."""

import base64
import io
import logging
from collections.abc import Iterator

from openai import OpenAI
from PIL import Image

from app.core.config import Config
from app.models.stylize import PROMPTS, Style

log = logging.getLogger(__name__)

_client: OpenAI | None = None


def _client_lazy() -> OpenAI:
    """Se crea al primer uso, no al importar: /health debe seguir vivo sin key."""
    global _client
    if _client is None:
        Config.OpenAI.validate()
        _client = OpenAI(api_key=Config.OpenAI.API_KEY, timeout=Config.OpenAI.TIMEOUT_S)
    return _client


def stylize_stream(cuadrado_jpeg: bytes, style: Style) -> Iterator[tuple[Image.Image, bool]]:
    """Estiliza un JPEG **ya recortado a cuadrado**, soltando versiones parciales.

    Devuelve (imagen, es_final). El modelo tarda unos 20 s en la imagen buena,
    pero suelta un primer borrador reconocible a los ~7 s: enseñarlo mientras
    llega el resto es la diferencia entre mirar una barra de carga y ver la foto
    dibujarse. La generacion no va mas rapida, lo que baja es la espera en
    blanco.

    `input_fidelity="high"` es lo que hace que se siga reconociendo a la persona
    de la foto; sin eso el modelo tiende a inventarse otra cara.
    """
    flujo = _client_lazy().images.edit(
        model=Config.OpenAI.IMAGE_MODEL,
        image=("foto.jpg", cuadrado_jpeg, "image/jpeg"),
        prompt=PROMPTS[style],
        size=Config.OpenAI.SIZE,
        quality=Config.OpenAI.QUALITY,
        input_fidelity=Config.OpenAI.INPUT_FIDELITY,
        n=1,
        stream=True,
        partial_images=Config.OpenAI.PARTIAL_IMAGES,
    )

    for evento in flujo:
        if evento.type == "image_edit.partial_image":
            log.info("parcial %d recibida", evento.partial_image_index)
            yield Image.open(io.BytesIO(base64.b64decode(evento.b64_json))), False
        elif evento.type == "image_edit.completed":
            if evento.usage is not None:
                # Las parciales se facturan como salida: aqui se ve el precio
                # real de haber pedido PARTIAL_IMAGES borradores.
                log.info("tokens: %s entrada, %s salida (%d parciales pedidas)",
                         evento.usage.input_tokens, evento.usage.output_tokens,
                         Config.OpenAI.PARTIAL_IMAGES)
            yield Image.open(io.BytesIO(base64.b64decode(evento.b64_json))), True
