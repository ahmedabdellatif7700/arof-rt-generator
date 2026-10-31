# arof-stream: real-time A-RoF sample generator, arm64 (Raspberry Pi, 64-bit OS).
#   docker build -t arof-stream .
#   docker build --build-arg MCPU=cortex-a76 -t arof-stream .    # Pi 5
# Base image pinned by digest (debian:bookworm-slim, multi-arch index, pulled 2026-10-31).
ARG BASE=debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3

# ---- build stage ----
FROM --platform=linux/arm64 ${BASE} AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config libfftw3-dev nlohmann-json3-dev \
 && rm -rf /var/lib/apt/lists/*
# cortex-a72: Pi 4 (this board); cortex-a76: Pi 5
ARG MCPU=cortex-a72
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAROF_MCPU=${MCPU} \
 && cmake --build build -j"$(nproc)" \
 && ctest --test-dir build --output-on-failure

# ---- runtime stage ----
FROM --platform=linux/arm64 ${BASE}
RUN apt-get update \
 && apt-get install -y --no-install-recommends libfftw3-double3 \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/arof_stream /usr/local/bin/arof_stream
COPY config /config
EXPOSE 5001
ENTRYPOINT ["/usr/local/bin/arof_stream"]
CMD ["--config", "/config/default.json"]
