#!/usr/bin/env bash
# Arranca el proxy de estilizacion.
#
# --timeout-keep-alive es imprescindible, no un adorno: el ESP32 reutiliza una
# sola conexion TCP para ahorrarse ~880 ms de handshake en cada foto, y quien la
# mantiene viva es su comprobacion de salud cada 20 s. Con los 5 s que trae
# uvicorn por defecto, el servidor cierra la conexion entre comprobacion y
# comprobacion y la reutilizacion no llega a ocurrir nunca.
set -euo pipefail
cd "$(dirname "$0")"
exec .venv/bin/uvicorn app.main:app \
    --host 0.0.0.0 --port 8000 \
    --timeout-keep-alive 75 \
    "$@"
