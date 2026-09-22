#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: compiles WindNerd firmware inside the Docker image.
# Mount the firmware dir (containing src/ and lib/) at /firmware.

FIRMWARE_DIR="/firmware"
SRC_DIR="${FIRMWARE_DIR}/src"
BUILD_DIR="${FIRMWARE_DIR}/build"

# FQBN for Generic STM32G031F8Px on stm32duino
FQBN="STMicroelectronics:stm32:GenG0:pnum=GENERIC_G031F8PX"

# Install WindNerd Core library from mounted submodule
LIB_SRC="${FIRMWARE_DIR}/lib/Windnerd-Core"
LIB_DST="/arduino/user/libraries/Windnerd_Core"
if [ -d "${LIB_SRC}" ]; then
  rm -rf "${LIB_DST}"
  cp -r "${LIB_SRC}" "${LIB_DST}"
fi

echo "=== WindNerd Core firmware build ==="
echo "FQBN: ${FQBN}"
echo "Source: ${SRC_DIR}"
echo "Output: ${BUILD_DIR}"
echo ""

# Compile
echo "--- Compiling ---"
arduino-cli compile \
  --fqbn "${FQBN}" \
  --build-path "${BUILD_DIR}" \
  --warnings default \
  "${SRC_DIR}"

echo ""
echo "=== Build complete ==="
ls -lh "${BUILD_DIR}"/*.bin "${BUILD_DIR}"/*.elf 2>/dev/null || echo "No output files found!"
