#!/usr/bin/env bash
set -euo pipefail

# Compile ESP32-C6 SuperMini firmware using the prebuilt CI/CD env image.
# Usage: ./build.sh
# Override image: ESP32C6_BUILD_IMAGE=esp32c6-build ./build.sh
# Output: build/src.ino.bin, build/src.ino.elf, build/src.ino.partitions.bin

FIRMWARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${ESP32C6_BUILD_IMAGE:-ghcr.io/thehonker/weathernerd-esp32c6-supermini-env:latest}"

echo "=== Pulling build image: $IMAGE_NAME ==="
docker pull "$IMAGE_NAME"

echo ""
echo "=== Compiling firmware ==="
docker run --rm -v "$FIRMWARE_DIR:/firmware" "$IMAGE_NAME" /usr/local/bin/compile.sh
