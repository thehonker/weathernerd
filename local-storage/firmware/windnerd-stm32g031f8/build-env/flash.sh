#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: flashes firmware to STM32G031F8 via ST-Link SWD.
# Requires the build/ directory to be populated (run compile.sh first).

OPENOCD="/arduino/data/packages/STMicroelectronics/tools/xpack-openocd/0.12.0-6/bin/openocd"
OPENOCD_SCRIPTS="/arduino/data/packages/STMicroelectronics/tools/xpack-openocd/0.12.0-6/openocd/scripts"
BIN="/firmware/build/src.ino.bin"

if [ ! -f "$BIN" ]; then
  echo "Error: $BIN not found. Compile first."
  exit 1
fi

echo "=== Flashing $BIN via ST-Link SWD ==="
"$OPENOCD" -s "$OPENOCD_SCRIPTS" \
  -f interface/stlink.cfg \
  -f target/stm32g0x.cfg \
  -c "program $BIN reset exit 0x08000000"
