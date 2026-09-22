#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: flashes ESP32-C6 via USB serial using esptool.
# Requires the build/ directory to be populated (run compile.sh first).
# Mount the firmware project dir at /project and pass USB through.

cd /project

# Find serial port
PORT="${ESP32_PORT:-}"
if [ -z "$PORT" ]; then
  for candidate in /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyACM1 /dev/ttyUSB1; do
    if [ -e "$candidate" ]; then
      PORT="$candidate"
      break
    fi
  done
fi
if [ -z "$PORT" ]; then
  echo "Error: No serial port found. Set ESP32_PORT explicitly."
  echo "Tried: /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyACM1 /dev/ttyUSB1"
  exit 1
fi

echo "=== Flashing via $PORT ==="
idf.py --port "$PORT" flash

echo ""
echo "=== Flash complete ==="
