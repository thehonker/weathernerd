# Build environment for WeatherNerd ESP32-C6 SuperMini firmware (ESP-IDF)
# Uses the official espressif/idf Docker image as base.
#
# Usage:
#   ./build.sh          (pulls GHCR image, compiles)
#   ./flash.sh          (flashes via USB serial)
#
# Or manually:
#   docker build -t esp32c6-build ./build-env
#   docker run --rm -v "$(pwd):/project" -w /project esp32c6-build /usr/local/bin/compile.sh

FROM docker.io/espressif/idf:release-v6.1

# Install serial port support for flashing
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        python3-serial \
    && rm -rf /var/lib/apt/lists/*

# Copy build/flash scripts
COPY compile.sh /usr/local/bin/compile.sh
RUN chmod +x /usr/local/bin/compile.sh
COPY flash.sh /usr/local/bin/flash.sh
RUN chmod +x /usr/local/bin/flash.sh

WORKDIR /project
