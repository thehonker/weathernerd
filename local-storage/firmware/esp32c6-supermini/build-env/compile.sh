#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: compiles ESP32-C6 firmware with ESP-IDF.
# Mount the firmware project dir at /project.

cd /project

echo "=== Building ESP32-C6 firmware with ESP-IDF ==="
idf.py set-target esp32c6
idf.py build

echo ""
echo "=== Build complete ==="
ls -lh build/esp32c6_weathernerd.bin build/esp32c6_weathernerd.elf 2>/dev/null || \
    ls -lh build/*.bin build/*.elf 2>/dev/null || echo "No output files found!"
