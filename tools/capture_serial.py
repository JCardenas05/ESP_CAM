#!/usr/bin/env python
"""Captura el log serie de la placa de forma NO interactiva.

`idf.py monitor` exige un TTY, asi que no sirve para scripts ni CI. Esto resetea
la placa (secuencia DTR/RTS del USB-JTAG nativo del ESP32-S3) y vuelca lo que
salga durante N segundos.

Uso:  python tools/capture_serial.py [segundos] [--no-reset] [--port /dev/ttyACM0]
Requiere pyserial: usar el interprete del venv de ESP-IDF.
"""

import argparse
import re
import sys
import time

import serial

ANSI = re.compile(r"\x1b\[[0-9;]*m")


def reset(ser: serial.Serial) -> None:
    """Hard reset por USB-JTAG: pulso en EN a traves de RTS.

    Hay que dejar primero DTR y RTS en bajo; un pulso de RTS en solitario,
    sin ese estado previo, no llega a resetear el chip.
    """
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(0.1)
    ser.setRTS(True)
    time.sleep(0.2)
    ser.setRTS(False)
    ser.setDTR(False)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("segundos", nargs="?", type=float, default=8.0)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    ser = None
    for _ in range(30):  # el puerto USB tarda en reaparecer tras un flash
        try:
            ser = serial.Serial(args.port, 115200, timeout=0.2)
            break
        except OSError:
            time.sleep(0.5)
    if ser is None:
        print(f"no se pudo abrir {args.port}", file=sys.stderr)
        return 1

    if not args.no_reset:
        reset(ser)
    ser.reset_input_buffer()

    fin = time.time() + args.segundos
    buf = b""
    while time.time() < fin:
        buf += ser.read(4096)
    ser.close()

    sys.stdout.write(ANSI.sub("", buf.decode("utf-8", "replace")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
