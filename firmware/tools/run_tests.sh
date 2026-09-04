#!/usr/bin/env bash
# Verificación de la matemática del Horizon Lock en el PC.
# Usa exactamente el mismo fc_horizon.h que se compila para el ESP32,
# así que lo que pasa aquí es lo que corre en la placa.
#
#   ./run_tests.sh
#
# Sólo hace falta g++. No requiere placa ni Arduino.
set -e
cd "$(dirname "$0")"
echo "Compilando test_horizon.cpp ..."
g++ -std=c++17 -O2 -Wall -Wextra -I../esp32s3_flexcam test_horizon.cpp -o /tmp/fc_test_horizon
echo
/tmp/fc_test_horizon
