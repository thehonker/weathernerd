# Build environment for WeatherNerd WindNerd Core firmware
# Target: STM32G031F8Px (WindNerd Core board)
#
# Usage:
#   ./build.sh
#
# Or manually:
#   docker build -t windnerd-build ./build-env
#   docker run --rm -v "$(pwd):/firmware" windnerd-build
#
# Output:
#   build/src.ino.bin  (flashable via ST-Link SWD)
#   build/src.ino.elf  (for debugging)

FROM docker.io/ubuntu:24.04

ARG ARDUINO_CLI_VERSION=1.5.1
ARG STM32DUINO_VERSION=3.0.0

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
    && rm -rf /var/lib/apt/lists/*

# Install arduino-cli
RUN curl -fsSL https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz \
        | tar xz -C /usr/local/bin arduino-cli

# Configure arduino-cli
RUN arduino-cli config init && \
    arduino-cli config set board_manager.additional_urls \
        https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json

# Install stm32duino core (includes ARM GCC toolchain)
RUN arduino-cli core update-index && \
    arduino-cli core install STMicroelectronics:stm32@${STM32DUINO_VERSION}

# WindNerd Core library is installed at runtime by compile.sh from the
# mounted submodule at /firmware/lib/Windnerd-Core. This keeps the image
# independent of the submodule state — no rebuild needed when the
# submodule is updated.

# Compile script
COPY compile.sh /usr/local/bin/compile.sh
RUN chmod +x /usr/local/bin/compile.sh

# Flash script
COPY flash.sh /usr/local/bin/flash.sh
RUN chmod +x /usr/local/bin/flash.sh

WORKDIR /firmware

ENTRYPOINT ["/bin/bash"]
