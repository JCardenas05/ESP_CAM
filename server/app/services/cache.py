"""Caché de resultados por identificador de captura.

Sin esto, cada reintento del ESP32 vuelve a llamar al modelo y se paga otra vez
la misma foto. En la Etapa 7 hubo un ciclo que necesitó 3 intentos y generó —y
cobró— 3 imágenes idénticas, de las que el dispositivo solo llegó a ver una.

La foto ya está subida cuando llega el reintento, así que la clave la pone el
firmware: el mismo `capture_id` para los tres intentos de una misma captura.
"""

from collections import OrderedDict

_MAX_ENTRADAS = 24

_cache: "OrderedDict[str, bytes]" = OrderedDict()


def get(capture_id: str) -> bytes | None:
    if not capture_id:
        return None
    datos = _cache.get(capture_id)
    if datos is not None:
        _cache.move_to_end(capture_id)
    return datos


def put(capture_id: str, datos: bytes) -> None:
    if not capture_id:
        return
    _cache[capture_id] = datos
    _cache.move_to_end(capture_id)
    while len(_cache) > _MAX_ENTRADAS:
        _cache.popitem(last=False)


def size() -> int:
    return len(_cache)
