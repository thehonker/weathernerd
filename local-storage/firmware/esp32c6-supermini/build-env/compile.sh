#!/usr/bin/env bash
set -euo pipefail

# Container entrypoint: compiles ESP32-C6 firmware inside the Docker image.
# Mount the firmware dir (containing src/ and lib/) at /firmware.

FIRMWARE_DIR="/firmware"
SRC_DIR="${FIRMWARE_DIR}/src"
BUILD_DIR="${FIRMWARE_DIR}/build"

# FQBN for ESP32C6 Dev Module (SuperMini uses the standard C6 dev module config)
FQBN="esp32:esp32:esp32c6"

# Install any libraries from mounted lib/ directory
LIB_DIR="${FIRMWARE_DIR}/lib"
if [ -d "${LIB_DIR}" ]; then
  for lib in "${LIB_DIR}"/*; do
    if [ -d "${lib}" ]; then
      lib_name=$(basename "${lib}")
      LIB_DST="/arduino/user/libraries/${lib_name}"
      rm -rf "${LIB_DST}"
      cp -r "${lib}" "${LIB_DST}"
    fi
  done
fi

echo "=== ESP32-C6 SuperMini firmware build ==="
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
