FROM python:3.11-slim

ENV PIP_NO_CACHE_DIR=1 \
    HOME=/tmp \
    PLATFORMIO_CORE_DIR=/platformio \
    PROJECT_DIR=/workspace \
    OUTPUT_DIR=/output

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      ca-certificates \
      git \
      build-essential \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio \
    && mkdir -p /platformio /output \
    && chmod 0777 /platformio /output

COPY docker/build-firmware.sh /usr/local/bin/build-firmware
RUN chmod +x /usr/local/bin/build-firmware

WORKDIR /workspace
ENTRYPOINT ["build-firmware"]
