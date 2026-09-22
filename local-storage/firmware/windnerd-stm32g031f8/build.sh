#!/usr/bin/env bash
set -euo pipefail

# Compile WindNerd STM32G031F8 firmware using the prebuilt CI/CD env image.
# Usage: ./build.sh
# Override image: WINDNERD_BUILD_IMAGE=windnerd-build ./build.sh
# Output: build/src.ino.bin, build/src.ino.elf

FIRMWARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${WINDNERD_BUILD_IMAGE:-ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env:latest}"

echo "=== Pulling build image: $IMAGE_NAME ==="
docker pull "$IMAGE_NAME"

echo ""
echo "=== Compiling firmware ==="
docker run --rm -v "$FIRMWARE_DIR:/firmware" "$IMAGE_NAME" /usr/local/bin/compile.sh
