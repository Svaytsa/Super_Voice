# syntax=docker/dockerfile:1.5

FROM ubuntu:22.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
        cmake \
        git \
        ninja-build \
        pkg-config \
        libzstd-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release


FROM ubuntu:22.04 AS runtime

ARG ROLE=server
ENV ROLE=${ROLE}
ENV CLIENT_ARGS=""
ENV SERVER_ARGS=""

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libzstd1 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/local_ai_model

COPY --from=builder /src/build/Client/client_app ./client_app
COPY --from=builder /src/build/Server/server_app ./server_app

ENTRYPOINT ["/bin/sh", "-c", "if [ \"$ROLE\" = \"client\" ]; then exec /opt/local_ai_model/client_app $CLIENT_ARGS; else exec /opt/local_ai_model/server_app $SERVER_ARGS; fi"]

