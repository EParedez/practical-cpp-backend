# Multi-stage build for the blog backend.
# Stage 1: compile from source.
FROM ubuntu:24.04 AS builder

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git ca-certificates \
    libmongoc-dev libmongocxx-dev libgrpc++-dev protobuf-compiler-grpc \
    libprotobuf-dev libgtest-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF && \
    cmake --build build -j"$(nproc)"

# Stage 2: minimal runtime.
FROM ubuntu:24.04 AS runtime

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libmongocxx3 libgrpc++1 libprotobuf32 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/blog_http_server /app/blog_http_server
COPY --from=builder /src/build/blog_grpc_server /app/blog_grpc_server

ENV MONGODB_URI=mongodb://mongo:27017
EXPOSE 8080 50051

CMD ["./blog_http_server", "0.0.0.0", "8080", "mongodb://mongo:27017"]
