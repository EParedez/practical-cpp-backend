#!/usr/bin/env bash
# Setup script for the Practical C++ Backend project.
# Installs dependencies (macOS Homebrew), configures and builds, and runs tests.
set -euo pipefail

cd "$(dirname "$0")"

echo "==> Checking for Homebrew..."
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Please install it from https://brew.sh"
  exit 1
fi

echo "==> Installing dependencies..."
brew install mongo-cxx-driver grpc googletest

echo "==> Setting up a local MongoDB (data dir: /tmp/practical-cpp-mongo)..."
if ! pgrep -x mongod >/dev/null 2>&1; then
  mkdir -p /tmp/practical-cpp-mongo
  mongod --dbpath /tmp/practical-cpp-mongo --port 27017 --fork \
    --logpath /tmp/practical-cpp-mongo/mongod.log
fi

echo "==> Configuring build..."
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/grpc;/opt/homebrew/opt/mongo-cxx-driver;/opt/homebrew/opt/bsoncxx;/opt/homebrew/opt/mongo-c-driver;/opt/homebrew/opt/googletest"

echo "==> Building..."
cmake --build build -j"$(sysctl -n hw.ncpu)"

echo "==> Running tests..."
./build/tests/run_tests

echo ""
echo "Setup complete. Start servers with:"
echo "  ./build/blog_http_server 0.0.0.0 8080"
echo "  ./build/blog_grpc_server  0.0.0.0:50051"
echo "  ./build/blog_grpc_client  localhost:50051"
