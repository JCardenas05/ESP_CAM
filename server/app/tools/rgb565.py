"""Conversión al formato crudo que la LCD del ESP32-S3 traga sin decodificar.

El ST7789 quiere RGB565 **big-endian**. Es el mismo orden que produce
`LCD_RGB565()` en el firmware, así que el ESP32 puede volcar estos bytes a la
pantalla tal cual, sin tocar ni uno.
"""

from PIL import Image


def to_rgb565_be(img: Image.Image, width: int, height: int) -> bytes:
    """Reescala a width x height (recortando al centro) y devuelve RGB565 big-endian."""
    rgb = _fit_center(img.convert("RGB"), width, height)
    raw = rgb.tobytes()
    out = bytearray(width * height * 2)

    for i in range(width * height):
        r, g, b = raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[2 * i] = v >> 8
        out[2 * i + 1] = v & 0xFF

    return bytes(out)


def _fit_center(img: Image.Image, width: int, height: int) -> Image.Image:
    """Escala conservando proporción y recorta el sobrante por el centro.

    La IA devuelve imágenes cuadradas y la pantalla es 240x320, así que sin
    recorte la cara saldría deformada.
    """
    escala = max(width / img.width, height / img.height)
    nuevo = (max(width, round(img.width * escala)), max(height, round(img.height * escala)))
    img = img.resize(nuevo, Image.LANCZOS)

    izq = (img.width - width) // 2
    arriba = (img.height - height) // 2
    return img.crop((izq, arriba, izq + width, arriba + height))
