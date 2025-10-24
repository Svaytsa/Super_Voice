# syntax=docker/dockerfile:1

FROM ubuntu:22.04 AS build

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        cmake \
        git \
        curl \
        pkg-config \
        libssl-dev \
        zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j && \
    cmake --install build --prefix /opt/superapi

FROM ubuntu:22.04 AS runtime

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        ca-certificates \
        libssl3 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /opt/superapi

COPY --from=build /opt/superapi /opt/superapi
COPY config ./config
COPY .env.example ./

ENV HOST=0.0.0.0
ENV PORT=8080
ENV DRY_RUN=false
ENV LOG_LEVEL=info

EXPOSE 8080

ENTRYPOINT ["/opt/superapi/bin/superapi_server"]
