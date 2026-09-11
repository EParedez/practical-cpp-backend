#!/usr/bin/env bash
set -euo pipefail

driver_version="${MONGO_CXX_DRIVER_VERSION:-4.5.0}"
install_prefix="${MONGO_CXX_DRIVER_PREFIX:-$HOME/.local/mongo-cxx-driver}"

if [[ -f "$install_prefix/lib/cmake/mongocxx-${driver_version}/mongocxxConfig.cmake" ]]; then
  echo "MongoDB C++ Driver ${driver_version} restored from cache."
  exit 0
fi

source_dir="$(mktemp -d)"
trap 'rm -rf "$source_dir"' EXIT

git clone --branch "r${driver_version}" --depth 1 \
  https://github.com/mongodb/mongo-cxx-driver.git "$source_dir/source"
cmake -S "$source_dir/source" -B "$source_dir/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" \
  -DCMAKE_PREFIX_PATH=/usr \
  -DBUILD_VERSION="$driver_version" \
  -DBUILD_SHARED_AND_STATIC_LIBS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DENABLE_TESTS=OFF \
  -DENABLE_EXAMPLES=OFF
cmake --build "$source_dir/build" --parallel 2
cmake --install "$source_dir/build"
