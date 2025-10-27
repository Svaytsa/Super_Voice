# syntax=docker/dockerfile:1.7

FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
        build-essential \
        ca-certificates \
        cmake \
        git \
        pkg-config \
        libssl-dev \
        zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY CMakeLists.txt .
COPY include ./include
COPY src ./src
COPY config ./config
COPY third_party ./third_party
COPY tests ./tests

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/superapi && \
    cmake --build build --target install -j

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
        ca-certificates \
        curl \
        libssl3 \
        tzdata && \
    rm -rf /var/lib/apt/lists/* && \
    useradd --system --create-home --home-dir /home/superapi superapi

WORKDIR /opt/superapi

COPY --from=build /opt/superapi /opt/superapi
COPY config ./config
COPY .env.example ./

RUN chown -R superapi:superapi /opt/superapi /home/superapi

ENV HOST=0.0.0.0 \
    PORT=8080 \
    DRY_RUN=false \
    LOG_LEVEL=info

EXPOSE 8080

USER superapi

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS "http://127.0.0.1:${PORT}/health" || exit 1

ENTRYPOINT ["/opt/superapi/bin/superapi_server"]
