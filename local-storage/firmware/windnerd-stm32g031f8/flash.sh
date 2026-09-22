#!/usr/bin/env bash
set -euo pipefail

# Flash firmware to STM32G031F8 via ST-Link SWD.
#
# Usage:
#   ./flash.sh              Flash locally built build/src.ino.bin
#   ./flash.sh --ci         Download latest release and flash that
#   ./flash.sh --ci <sha>   Download release for a specific commit sha
#
# Override image: WINDNERD_BUILD_IMAGE=windnerd-build ./flash.sh
# Requires: ST-Link V2 connected (CLK, DIO, RST, GND)

FIRMWARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${WINDNERD_BUILD_IMAGE:-ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env:latest}"
REPO="thehonker/weathernerd"
RELEASE_PREFIX="windnerd-stm32g031f8"
BIN_PATH="$FIRMWARE_DIR/build/src.ino.bin"

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
    # Resolve latest release tag via API (no auth needed for public repo)
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
  curl -fsSL -o "$FIRMWARE_DIR/build/src.ino.bin" "$DOWNLOAD_URL/src.ino.bin"
  curl -fsSL -o "$FIRMWARE_DIR/build/src.ino.elf" "$DOWNLOAD_URL/src.ino.elf"
  BIN_PATH="$FIRMWARE_DIR/build/src.ino.bin"
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

echo "=== Flashing $BIN_PATH via ST-Link SWD ==="
docker run --rm \
  --device /dev/bus/usb \
  -v "$FIRMWARE_DIR:/firmware" \
  "$IMAGE_NAME" \
  /usr/local/bin/flash.sh
