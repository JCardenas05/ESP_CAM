#!/usr/bin/env bash
# Carga el entorno ESP-IDF v5.3.1 forzando el venv de Python 3.12.
# El venv "idf5.3_py3.13_env" quedó roto tras la actualización del sistema a Python 3.14.
# Uso:  source ./idf.sh
export IDF_PATH="/home/jcardenas05/esp/v5.3.1/esp-idf"
export IDF_PYTHON_ENV_PATH="/home/jcardenas05/.espressif/python_env/idf5.3_py3.12_env"
source "$IDF_PATH/export.sh"
export ESPPORT="${ESPPORT:-/dev/ttyACM0}"
