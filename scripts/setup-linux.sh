#!/usr/bin/env bash
# Native build setup for supported Ubuntu releases. Build dependencies are installed
# on the host; MongoDB runs in a temporary Docker container for integration tests.
set -euo pipefail

repository_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-$repository_dir/build-linux}"
driver_prefix="${MONGO_CXX_DRIVER_PREFIX:-$repository_dir/.deps/mongo-cxx-driver}"
build_jobs="${BUILD_JOBS:-2}"
mongo_container="practical-cpp-backend-test-mongo-$$"

if [[ ! -r /etc/os-release ]]; then
  echo "Cannot identify this Linux distribution. Ubuntu 22.04 and 24.04 are supported." >&2
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" || ( "${VERSION_ID:-}" != "22.04" && "${VERSION_ID:-}" != "24.04" ) ]]; then
  echo "Native setup supports Ubuntu 22.04 and 24.04; detected ${PRETTY_NAME:-unknown}." >&2
  echo "Use ./scripts/smoke_test.sh for the distribution-independent Docker workflow." >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  privilege_command=()
elif command -v sudo >/dev/null 2>&1; then
  privilege_command=(sudo)
else
  echo "sudo is required to install Ubuntu build dependencies." >&2
  exit 1
fi

echo "==> Installing native build dependencies on ${PRETTY_NAME}..."
"${privilege_command[@]}" apt-get update
"${privilege_command[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential ca-certificates cmake git libgrpc++-dev libgtest-dev \
  libmongoc-dev libprotobuf-dev libssl-dev pkg-config protobuf-compiler-grpc

echo "==> Installing the pinned MongoDB C++ driver into $driver_prefix..."
MONGO_CXX_DRIVER_PREFIX="$driver_prefix" "$repository_dir/scripts/install_mongo_cxx_driver.sh"

echo "==> Configuring and building..."
cmake -S "$repository_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_PREFIX_PATH="$driver_prefix"
cmake --build "$build_dir" --parallel "$build_jobs"

echo "==> Running unit tests..."
ctest --test-dir "$build_dir" -L unit --output-on-failure

if [[ "${SKIP_INTEGRATION_TESTS:-0}" == "1" ]]; then
  echo "==> Integration tests skipped because SKIP_INTEGRATION_TESTS=1."
  exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "Docker is required for MongoDB-backed integration tests." >&2
  echo "Install Docker or rerun with SKIP_INTEGRATION_TESTS=1." >&2
  exit 1
fi

cleanup() {
  docker rm --force "$mongo_container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "==> Starting an isolated MongoDB container..."
docker run --detach --rm --name "$mongo_container" \
  --publish 127.0.0.1:27017:27017 mongo:8.0 >/dev/null

mongo_ready=0
for _ in $(seq 1 40); do
  if docker exec "$mongo_container" mongosh --quiet \
    --eval 'quit(db.adminCommand({ping: 1}).ok ? 0 : 1)' >/dev/null 2>&1; then
    mongo_ready=1
    break
  fi
  sleep 1
done

if [[ "$mongo_ready" != "1" ]]; then
  echo "MongoDB did not become ready within 40 seconds." >&2
  docker logs "$mongo_container" >&2 || true
  exit 1
fi

echo "==> Running MongoDB-backed integration tests..."
ctest --test-dir "$build_dir" -L integration --output-on-failure --timeout 30

cleanup
trap - EXIT
echo "Linux setup complete."
