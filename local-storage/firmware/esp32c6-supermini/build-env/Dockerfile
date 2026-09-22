# Build environment for WeatherNerd ESP32-C6 SuperMini firmware
# Target: ESP32-C6 SuperMini (ESP32C6 Dev Module)
#
# Usage:
#   ./build.sh          (pulls GHCR image, compiles)
#   ./flash.sh          (flashes via USB serial)
#
# Or manually:
#   docker build -t esp32c6-build ./build-env
#   docker run --rm -v "$(pwd):/firmware" esp32c6-build /usr/local/bin/compile.sh
#
# Output:
#   build/src.ino.bin   (flashable via esptool / USB)
#   build/src.ino.elf   (for debugging)
#   build/src.ino.partitions.bin

FROM docker.io/ubuntu:24.04

ARG ARDUINO_CLI_VERSION=1.5.1
ARG ESP32_CORE_VERSION=3.3.12

ENV DEBIAN_FRONTEND=noninteractive
ENV ARDUINO_DIRECTORIES_DATA=/arduino/data
ENV ARDUINO_DIRECTORIES_DOWNLOADS=/arduino/downloads
ENV ARDUINO_DIRECTORIES_USER=/arduino/user

# Create Arduino directories
RUN mkdir -p /arduino/data /arduino/downloads /arduino/user/libraries

# Install base tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        xz-utils \
        git \
        make \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# Install arduino-cli
RUN curl -fsSL https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz \
        | tar xz -C /usr/local/bin arduino-cli

# Configure arduino-cli
RUN arduino-cli config init && \
    arduino-cli config set board_manager.additional_urls \
        https://espressif.github.io/arduino-esp32/package_esp32_index.json

# Install ESP32 Arduino core (includes RISC-V GCC toolchain + esptool)
RUN arduino-cli core update-index && \
    arduino-cli core install esp32:esp32@${ESP32_CORE_VERSION}

# Compile script (container entrypoint)
COPY compile.sh /usr/local/bin/compile.sh
RUN chmod +x /usr/local/bin/compile.sh

# Flash script (container entrypoint)
COPY flash.sh /usr/local/bin/flash.sh
RUN chmod +x /usr/local/bin/flash.sh

WORKDIR /firmware

ENTRYPOINT ["/bin/bash"]
