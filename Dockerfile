# syntax=docker/dockerfile:1
# Multi-stage build for the blog backend.
# Stage 1: compile from source.
ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION} AS builder

ARG MONGO_CXX_DRIVER_VERSION=4.5.0

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git ca-certificates \
    libmongoc-dev libgrpc++-dev protobuf-compiler-grpc libssl-dev \
    libprotobuf-dev libgtest-dev && \
    rm -rf /var/lib/apt/lists/*

RUN git clone --branch "r${MONGO_CXX_DRIVER_VERSION}" --depth 1 \
      https://github.com/mongodb/mongo-cxx-driver.git /tmp/mongo-cxx-driver && \
    cmake -S /tmp/mongo-cxx-driver -B /tmp/mongo-cxx-driver/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/mongo-cxx-driver \
      -DCMAKE_PREFIX_PATH=/usr \
      -DBUILD_VERSION="${MONGO_CXX_DRIVER_VERSION}" \
      -DBUILD_SHARED_AND_STATIC_LIBS=OFF \
      -DBUILD_SHARED_LIBS=ON \
      -DENABLE_TESTS=OFF \
      -DENABLE_EXAMPLES=OFF && \
    cmake --build /tmp/mongo-cxx-driver/build --parallel 2 && \
    cmake --install /tmp/mongo-cxx-driver/build && \
    rm -rf /tmp/mongo-cxx-driver

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
      -DENABLE_WARNINGS_AS_ERRORS=ON \
      -DCMAKE_PREFIX_PATH=/opt/mongo-cxx-driver && \
    cmake --build build -j"$(nproc)" && \
    cmake --install build --prefix /opt/blog

# Stage 2: portable runtime. Resolve Ubuntu ABI package names from stable development
# package metadata instead of hard-coding release-specific names.
FROM ubuntu:${UBUNTU_VERSION} AS runtime

RUN apt-get update && \
    runtime_packages="$(apt-cache depends libgrpc++-dev libprotobuf-dev libssl-dev | \
      awk '$1 == "Depends:" {gsub(/[<>]/, "", $2); \
        if ($2 ~ /^libgrpc[+][+][0-9]/ || $2 ~ /^libprotobuf[0-9]/ || \
            $2 ~ /^libssl[0-9]/) print $2}' | sort -u)" && \
    test -n "$runtime_packages" && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates curl libsasl2-2 libsnappy1v5 libzstd1 $runtime_packages && \
    groupadd --gid 10001 app && \
    useradd --uid 10001 --gid app --no-create-home --shell /usr/sbin/nologin app && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder --chown=app:app /opt/blog/bin/blog_http_server /app/blog_http_server
COPY --from=builder --chown=app:app /opt/blog/bin/blog_grpc_server /app/blog_grpc_server
COPY --from=builder /opt/mongo-cxx-driver/lib/*.so* /opt/mongo-cxx-driver/lib/

ENV MONGODB_URI=mongodb://mongo:27017
ENV BLOG_DATABASE=blog
ENV MONGO_MIN_POOL_SIZE=1
ENV MONGO_MAX_POOL_SIZE=20
ENV MONGO_SERVER_SELECTION_TIMEOUT_MS=5000
ENV MONGO_CONNECT_TIMEOUT_MS=5000
ENV CACHE_CAPACITY=128
ENV CACHE_TTL_SECONDS=60
ENV HTTP_HOST=0.0.0.0
ENV PORT=8080
ENV GRPC_ADDRESS=0.0.0.0:50051
ENV GRPC_METRICS_HOST=0.0.0.0
ENV GRPC_METRICS_PORT=9090
ENV LD_LIBRARY_PATH=/opt/mongo-cxx-driver/lib
EXPOSE 8080 50051 9090

USER app
STOPSIGNAL SIGTERM

CMD ["./blog_http_server"]
