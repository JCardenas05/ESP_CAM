import io
import logging
import time
from datetime import datetime
from pathlib import Path

from fastapi import APIRouter, Depends, File, Form, HTTPException, Response, UploadFile
from fastapi.responses import StreamingResponse
from PIL import Image

from app.core.auth import requiere_token
from app.core.config import Config
from app.models.stylize import Style
from app.services import cache
from app.services.openai_image import stylize_stream
from app.tools.framing import center_square
from app.tools.rgb565 import to_rgb565_be

log = logging.getLogger(__name__)
router = APIRouter()

DATA = Path(__file__).resolve().parents[2] / "data"


@router.get("/health")
def health() -> dict:
    """Objetivo de validación de la Etapa 5: el ESP32 solo comprueba que responde."""
    return {
        "ok": True,
        "environment": Config.ENVIRONMENT,
        "frame_bytes": Config.LCD.frame_bytes(),
    }


@router.post("/echo", dependencies=[Depends(requiere_token)])
async def echo(image: UploadFile = File(...), style: str = Form("test")) -> Response:
    """Prueba de red: mismo formato que /stylize pero sin llamar a la IA.

    Sirve para medir el camino ESP32 -> WiFi -> servidor por separado del
    modelo, y para distinguir un problema de red de uno del proxy.
    """
    crudo = await image.read()
    lado = Config.LCD.IMAGE
    # Degradado sintetico en RGB565 big-endian, del mismo tamano que un resultado real
    datos = bytearray(lado * lado * 2)
    for y in range(lado):
        for x in range(lado):
            v = ((x * 31 // lado) << 11) | ((y * 63 // lado) << 5) | 0x0F
            i = (y * lado + x) * 2
            datos[i] = v >> 8
            datos[i + 1] = v & 0xFF
    log.info("/echo: recibidos %d bytes, devueltos %d", len(crudo), len(datos))
    return Response(content=bytes(datos), media_type="application/octet-stream",
                    headers={"X-Received": str(len(crudo))})


@router.post("/stylize", dependencies=[Depends(requiere_token)])
async def stylize(
    image: UploadFile = File(...),
    style: Style = Form(Style.ANIME),
    capture_id: str = Form(""),
) -> Response:
    """Recibe el JPEG de la cámara y devuelve RGB565 big-endian listo para la LCD.

    El ESP32 no decodifica nada: escribe los bytes de la respuesta directamente
    en el panel. Por eso la respuesta tiene un tamaño fijo y conocido.
    """
    t0 = time.monotonic()

    # El reintento de una captura ya resuelta se sirve de caché: repetir la
    # llamada al modelo costaría dinero por una imagen que ya existe.
    guardado = cache.get(capture_id)
    if guardado is not None:
        await image.read()          # hay que drenar el cuerpo igualmente
        log.info("%s: servido de caché (%d bytes)", capture_id, len(guardado))
        return Response(
            content=guardado,
            media_type="application/octet-stream",
            headers={"X-Image-Side": str(Config.LCD.IMAGE), "X-Cache": "hit"},
        )

    crudo = await image.read()
    if not crudo:
        raise HTTPException(status_code=400, detail="No llegó ninguna imagen")

    try:
        foto = Image.open(io.BytesIO(crudo))
        foto.load()
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"La imagen no se pudo abrir: {exc}")

    marca = datetime.now().strftime("%Y%m%d-%H%M%S")

    # Mismo recorte que hace el visor del ESP32, o el resultado no coincidirá
    # con lo que el usuario encuadró.
    cuadrado = center_square(foto).convert("RGB")
    origen = DATA / "originales" / f"{marca}.jpg"
    cuadrado.save(origen, "JPEG", quality=92)

    buf = io.BytesIO()
    cuadrado.save(buf, "JPEG", quality=92)

    lado = Config.LCD.IMAGE
    esperados = Config.LCD.frame_bytes()

    def a_frame(imagen: Image.Image) -> bytes:
        datos = to_rgb565_be(imagen, lado, lado)
        if len(datos) != esperados:
            raise RuntimeError(f"Se generaron {len(datos)} bytes y la pantalla espera {esperados}")
        return datos

    # El primer borrador se pide AQUI, antes de abrir la respuesta en streaming.
    # Una vez enviado el primer byte ya no se puede cambiar el codigo HTTP, asi
    # que si el modelo va a fallar conviene que falle mientras todavia podemos
    # contestar un 502 en condiciones.
    generador = stylize_stream(buf.getvalue(), style)
    try:
        primera, primera_final = next(generador)
    except StopIteration:
        raise HTTPException(status_code=502, detail="El modelo no devolvió ninguna imagen")
    except Exception as exc:
        log.exception("falló la llamada al modelo")
        raise HTTPException(status_code=502, detail=f"El modelo de imagen falló: {exc}")

    def emitir():
        """Frames RGB565 encadenados: el ESP32 lee 115200 bytes, pinta y repite.

        No lleva Content-Length a proposito (va en chunked): el numero de
        borradores que suelta el modelo no esta garantizado, asi que el fin de
        la respuesta lo marca el cierre de la conexion.
        """
        ultima, es_final = primera, primera_final
        n = 0
        while True:
            yield a_frame(ultima)
            n += 1
            log.info("%s: frame %d enviado%s (%.1fs)", marca, n,
                     " [final]" if es_final else " [borrador]", time.monotonic() - t0)
            if es_final:
                break
            try:
                ultima, es_final = next(generador)
            except StopIteration:
                break

        # Solo el resultado definitivo se guarda y se cachea: un borrador
        # serviria una imagen a medio dibujar en el siguiente reintento.
        if es_final:
            ultima.save(DATA / "resultados" / f"{marca}_{style.value}.png")
            cache.put(capture_id, a_frame(ultima))
        log.info("%s: %d bytes de foto -> %s -> %d frames (total %.1fs)",
                 marca, len(crudo), style.value, n, time.monotonic() - t0)

    return StreamingResponse(
        emitir(),
        media_type="application/octet-stream",
        headers={
            "X-Image-Side": str(lado),
            "X-Frame-Bytes": str(esperados),
            "X-Style": style.value,
            "X-Capture": marca,
            "X-Cache": "miss",
        },
    )
