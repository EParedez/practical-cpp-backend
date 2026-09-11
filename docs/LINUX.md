# Linux Support

## Support Policy

Native builds are supported and continuously tested on Ubuntu 22.04 and Ubuntu 24.04,
on the `amd64` architecture. The container image is built explicitly for both
`linux/amd64` and `linux/arm64`.

Fedora, Arch Linux, and other distributions are supported through Docker and Docker
Compose. Their native package layouts and compiler combinations are not currently part
of the compatibility contract.

## Ubuntu 22.04 and 24.04 Setup

The helper installs native compiler and library dependencies with `apt`, builds the
pinned MongoDB C++ driver under the repository's ignored `.deps` directory, configures
a strict release build, and runs all tests:

```bash
./scripts/setup-linux.sh
```

MongoDB itself is not installed on the host. The helper starts a temporary `mongo:8.0`
container bound to `127.0.0.1:27017` for integration tests and removes it afterward.
Docker Engine must therefore be available. To build and run only the 51 unit tests:

```bash
SKIP_INTEGRATION_TESTS=1 ./scripts/setup-linux.sh
```

Optional overrides are:

| Variable | Default | Purpose |
| --- | --- | --- |
| `BUILD_DIR` | `build-linux` in the repository | CMake build directory |
| `BUILD_JOBS` | `2` | Parallel build jobs |
| `MONGO_CXX_DRIVER_PREFIX` | `.deps/mongo-cxx-driver` | Private driver installation prefix |
| `SKIP_INTEGRATION_TESTS` | `0` | Set to `1` to omit Docker-backed integration tests |

The script requires `sudo` when it is not run as root. It deliberately rejects other
distributions instead of guessing equivalent package names.

Ubuntu 22.04 packages expose gRPC through `pkg-config`, while Ubuntu 24.04 also ships
its CMake package configuration. The project supports both discovery paths and still
requires the packaged `grpc_cpp_plugin` executable.

## Manual Native Build

Install the Ubuntu dependencies:

```bash
sudo apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential ca-certificates cmake git libgrpc++-dev libgtest-dev \
  libmongoc-dev libprotobuf-dev libssl-dev pkg-config protobuf-compiler-grpc
```

Install the pinned MongoDB C++ driver and build:

```bash
export MONGO_CXX_DRIVER_PREFIX="$PWD/.deps/mongo-cxx-driver"
./scripts/install_mongo_cxx_driver.sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_PREFIX_PATH="$MONGO_CXX_DRIVER_PREFIX"
cmake --build build-linux --parallel 2
ctest --test-dir build-linux -L unit --output-on-failure
```

## Distribution-Independent Docker Workflow

On Fedora, Arch Linux, or any Linux distribution with a compatible Docker Engine and
the Compose plugin, use:

```bash
./scripts/smoke_test.sh
```

This builds the Ubuntu-based application image, starts MongoDB, HTTP, gRPC, and Nginx,
executes the authenticated HTTP smoke flow, and removes the isolated environment.

## Architecture Verification

CI builds and inspects the final image separately for `linux/amd64` and `linux/arm64`.
The release workflow publishes a single multi-platform manifest containing both. The
Dockerfile derives Ubuntu-version-specific runtime package names from stable
development-package metadata instead of hard-coding ABI versions, allowing the same
file to build with either `--build-arg UBUNTU_VERSION=22.04` or `24.04` without
shipping development headers in the final image.
