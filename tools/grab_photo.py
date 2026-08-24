#!/usr/bin/env python
"""Extrae por el puerto serie las fotos que el firmware vuelca en base64.

El firmware las enmarca entre `--- JPEG BEGIN len=N ---` y `--- JPEG END ---`.
Este script escucha, decodifica y guarda cada una como .jpg, comprobando que
los bytes son un JPEG de verdad (SOI FFD8 al principio, EOI FFD9 al final).

Uso:  python tools/grab_photo.py [segundos] [--no-reset] [--out DIR]
"""

import argparse
import base64
import re
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).parent))
from capture_serial import ANSI, reset  # noqa: E402

INICIO = re.compile(r"--- JPEG BEGIN len=(\d+) ---")
FIN = "--- JPEG END ---"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("segundos", nargs="?", type=float, default=25.0)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--out", default="capturas")
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    destino = Path(args.out)
    destino.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, 115200, timeout=0.2)
    if not args.no_reset:
        reset(ser)
    ser.reset_input_buffer()

    fin = time.time() + args.segundos
    buf = b""
    while time.time() < fin:
        buf += ser.read(8192)
    ser.close()

    texto = ANSI.sub("", buf.decode("utf-8", "replace"))
    guardadas = 0

    for m in INICIO.finditer(texto):
        declarado = int(m.group(1))
        corte = texto.find(FIN, m.end())
        if corte < 0:
            print(f"foto incompleta (se esperaban {declarado} bytes): "
                  f"no llegó la marca de fin", file=sys.stderr)
            continue

        b64 = "".join(texto[m.end():corte].split())
        try:
            datos = base64.b64decode(b64)
        except Exception as exc:
            print(f"base64 corrupto: {exc}", file=sys.stderr)
            continue

        guardadas += 1
        ruta = destino / f"foto_{guardadas:02d}.jpg"
        ruta.write_bytes(datos)

        ok_len = len(datos) == declarado
        ok_soi = datos[:2] == b"\xff\xd8"
        ok_eoi = datos[-2:] == b"\xff\xd9"
        print(f"{ruta}: {len(datos)} bytes "
              f"(declarados {declarado}{'' if ok_len else ' ¡NO COINCIDE!'}) "
              f"SOI={'ok' if ok_soi else 'FALTA'} EOI={'ok' if ok_eoi else 'FALTA'}")

    if guardadas == 0:
        print("no se encontró ninguna foto en el flujo serie", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
