#!/usr/bin/env bash
# Setup script for the Practical C++ Backend project.
# Installs dependencies with macOS Homebrew, including MongoDB Community Edition as
# a native host service when mongod is unavailable. Use scripts/smoke_test.sh instead
# when MongoDB should run only in Docker.
set -euo pipefail

cd "$(dirname "$0")/.."

echo "NOTE: setup.sh installs MongoDB Community Edition natively with Homebrew."
echo "      Use ./scripts/smoke_test.sh for a Docker-only MongoDB workflow."
echo

echo "==> Checking for Homebrew..."
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Please install it from https://brew.sh"
  exit 1
fi

echo "==> Installing dependencies..."
brew install cmake openssl@3 mongo-cxx-driver grpc protobuf googletest

if ! command -v mongod >/dev/null 2>&1; then
  echo "==> Installing MongoDB Community Edition..."
  brew tap mongodb/brew
  brew install mongodb-community@8.0
fi

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
ctest --test-dir build --output-on-failure

echo ""
echo "Setup complete. Start servers with:"
echo "  ./build/blog_http_server 0.0.0.0 8080"
echo "  ./build/blog_grpc_server  0.0.0.0:50051"
echo "  ./build/blog_grpc_client  localhost:50051"
