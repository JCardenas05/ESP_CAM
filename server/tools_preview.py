"""Reconstruye a PNG un .raw RGB565 big-endian devuelto por /stylize.

Sirve para mirar con los ojos lo que va a ver la pantalla del ESP32.
Uso:  .venv/bin/python tools_preview.py salida.raw salida.png [lado]
"""

import sys

from PIL import Image


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    origen, destino = sys.argv[1], sys.argv[2]
    lado = int(sys.argv[3]) if len(sys.argv) > 3 else 240

    datos = open(origen, "rb").read()
    if len(datos) != lado * lado * 2:
        print(f"tamaño inesperado: {len(datos)} bytes, se esperaban {lado * lado * 2}")
        return 1

    img = Image.new("RGB", (lado, lado))
    px = img.load()
    for i in range(lado * lado):
        v = (datos[2 * i] << 8) | datos[2 * i + 1]      # big-endian
        r = ((v >> 11) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x3F) * 255 // 63
        b = (v & 0x1F) * 255 // 31
        px[i % lado, i // lado] = (r, g, b)

    img.save(destino)
    print(f"{destino}: {lado}x{lado} reconstruido desde {len(datos)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
