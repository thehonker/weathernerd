#!/usr/bin/env bash
set -euo pipefail

# Flash firmware to ESP32-C6 SuperMini via USB serial.
#
# Usage:
#   ./flash.sh              Flash locally built firmware
#   ./flash.sh --ci         Download latest release and flash that
#   ./flash.sh --ci <sha>   Download release for a specific commit sha
#
# Override image: ESP32C6_BUILD_IMAGE=esp32c6-build ./flash.sh
# Override serial port: ESP32_PORT=/dev/ttyACM1 ./flash.sh
# Requires: ESP32-C6 SuperMini connected via USB

FIRMWARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${ESP32C6_BUILD_IMAGE:-ghcr.io/thehonker/weathnerd-esp32c6-supermini-env:latest}"
REPO="thehonker/weathernerd"
RELEASE_PREFIX="esp32c6-supermini"
BIN_PATH="$FIRMWARE_DIR/build/esp32c6_weathernerd.bin"

# --- Parse args ---
MODE="local"
COMMIT_SHA=""
case "${1:-}" in
  --ci)
    MODE="ci"
    COMMIT_SHA="${2:-}"
    ;;
  "")
    MODE="local"
    ;;
  *)
    echo "Usage: $0 [--ci [commit-sha]]"
    exit 1
    ;;
esac

# --- Download from GitHub release if requested ---
if [ "$MODE" = "ci" ]; then
  echo "=== Downloading firmware from GitHub releases ==="
  mkdir -p "$FIRMWARE_DIR/build"

  if [ -n "$COMMIT_SHA" ]; then
    TAG="$RELEASE_PREFIX-$COMMIT_SHA"
  else
    TAG=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
      | grep -oP '"tag_name":\s*"\K[^"]*' \
      | grep "^$RELEASE_PREFIX" \
      | head -1)
    if [ -z "$TAG" ]; then
      echo "Error: No release found. Build locally with ./build.sh"
      exit 1
    fi
  fi

  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG"
  echo "Downloading $TAG..."
  curl -fsSL -o "$FIRMWARE_DIR/build/esp32c6_weathernerd.bin" "$DOWNLOAD_URL/esp32c6_weathernerd.bin"
  curl -fsSL -o "$FIRMWARE_DIR/build/esp32c6_weathernerd.elf" "$DOWNLOAD_URL/esp32c6_weathernerd.elf"
  curl -fsSL -o "$FIRMWARE_DIR/build/bootloader.bin" "$DOWNLOAD_URL/bootloader.bin"
  curl -fsSL -o "$FIRMWARE_DIR/build/partition-table.bin" "$DOWNLOAD_URL/partition-table.bin"
  BIN_PATH="$FIRMWARE_DIR/build/esp32c6_weathernerd.bin"
fi

# --- Verify binary exists ---
if [ ! -f "$BIN_PATH" ]; then
  echo "Error: $BIN_PATH not found."
  if [ "$MODE" = "local" ]; then
    echo "Build first with ./build.sh, or use: ./flash.sh --ci"
  fi
  exit 1
fi

# --- Pull image and flash ---
echo "=== Pulling build image: $IMAGE_NAME ==="
docker pull "$IMAGE_NAME"

DOCKER_ARGS=(--rm -v "$FIRMWARE_DIR:/project" -w /project -u "$(id -u)" -e HOME=/tmp)
if [ -n "${ESP32_PORT:-}" ]; then
  DOCKER_ARGS+=(-e "ESP32_PORT=$ESP32_PORT" --device "$ESP32_PORT")
else
  DOCKER_ARGS+=(--device /dev/bus/usb)
fi

echo "=== Flashing $BIN_PATH via USB serial ==="
docker run "${DOCKER_ARGS[@]}" "$IMAGE_NAME" /usr/local/bin/flash.sh
