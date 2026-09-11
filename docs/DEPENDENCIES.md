# Dependency Provenance and Updates

Most dependencies are resolved by CMake from system packages. The exception is
cpp-httplib, which is intentionally vendored as a single header so the HTTP target has
no runtime package dependency.

## Vendored cpp-httplib

| Field | Value |
| --- | --- |
| Project | `yhirose/cpp-httplib` |
| File | `src/server/httplib.h` |
| Recorded version | `0.53.0` |
| License | MIT; retain the upstream notice in the header |
| Local integration | Header-only with `CPPHTTPLIB_OPENSSL_SUPPORT` |

The version is read from `CPPHTTPLIB_VERSION` near the beginning of the vendored
header. Do not modify the third-party header for application behavior; place adapters
and policy in `src/server/http_app.*` instead. The format job deliberately excludes
this file so project formatting never rewrites upstream code.

## Update Procedure

1. Review upstream release notes and security advisories for every version between the
   current and proposed releases.
2. Download the release-tagged `httplib.h` from the upstream repository. Do not use an
   unpinned branch snapshot.
3. Verify the downloaded file against the checksum or signed release material supplied
   by upstream, when available.
4. Confirm that the header still contains the expected version and MIT license notice.
5. Replace only `src/server/httplib.h`; do not run clang-format over it.
6. Configure a strict build and run unit and integration tests:

   ```bash
   cmake -S . -B build-update -DCMAKE_BUILD_TYPE=Release \
     -DENABLE_WARNINGS_AS_ERRORS=ON
   cmake --build build-update -j4
   ctest --test-dir build-update --output-on-failure --timeout 20
   ```

7. Run `./scripts/smoke_test.sh` to exercise the OpenSSL-enabled server in the final
   container topology.
8. Record the old version, new version, upstream release URL, checksum verification,
   and test results in the pull request.

## System and Container Dependencies

Local builds resolve OpenSSL, gRPC, Protocol Buffers, MongoDB C/C++ drivers, and
GoogleTest through CMake packages. CI pins the source-built MongoDB C++ driver version
in `scripts/install_mongo_cxx_driver.sh`. The Dockerfile pins its base images and the
MongoDB C++ driver; Docker Compose pins MongoDB and Nginx images.

Ubuntu 22.04 does not expose a gRPC CMake package configuration from its system
package, so the root CMake project falls back to the packaged `grpc++` pkg-config
module and locates `grpc_cpp_plugin` as a program. Ubuntu 24.04 uses the preferred
upstream-style CMake targets. Both paths are exercised by the CI matrix.

When updating any pinned dependency, change all duplicate version declarations in one
pull request, regenerate the SBOM through CI, and review the dependency and container
scans before merge. Never weaken a scan threshold merely to accept an update.
