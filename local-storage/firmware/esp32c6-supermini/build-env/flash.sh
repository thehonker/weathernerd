#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: flashes ESP32-C6 via USB serial using esptool.
# Requires the build/ directory to be populated (run compile.sh first).
# Mount the firmware dir at /firmware and pass the serial device through.

FIRMWARE_DIR="/firmware"
BUILD_DIR="${FIRMWARE_DIR}/build"
BIN="${BUILD_DIR}/src.ino.bin"
PARTITIONS="${BUILD_DIR}/src.ino.partitions.bin"
BOOTLOADER="${BUILD_DIR}/src.ino.bootloader.bin"

# Find esptool inside the ESP32 core
ESPTOOL=$(find /arduino/data/packages/esp32/tools/esptool_py -name 'esptool.py' | head -1)
if [ -z "$ESPTOOL" ]; then
  echo "Error: esptool.py not found in ESP32 core"
  exit 1
fi

# Serial port — try common device names
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

if [ ! -f "$BIN" ]; then
  echo "Error: $BIN not found. Compile first."
  exit 1
fi

echo "=== Flashing $BIN via $PORT ==="
python3 "$ESPTOOL" \
  --chip esp32c6 \
  --port "$PORT" \
  --baud 921600 \
  write_flash \
  0x0 "$BOOTLOADER" \
  0x8000 "$PARTITIONS" \
  0x10000 "$BIN"

echo ""
echo "=== Flash complete ==="
