#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test -f include/local_config.h || {
  echo "Falta include/local_config.h. Copie include/local_config.example.h y configure Wi-Fi/backend."
  exit 2
}
python3 -m platformio run --environment panel_4848s040 --target upload
python3 -m platformio device monitor --baud 115200
