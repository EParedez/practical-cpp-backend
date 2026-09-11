# Practical C++ Backend Programming

A production-oriented C++17 blog backend assembled from the examples in *Practical
C++ Backend Programming* by Justin Barbara. It exposes the same MongoDB-backed domain
through HTTP and gRPC and includes caching, authentication, observability, tests,
containers, and a guarded release pipeline.

Spanish translations of the original overview and architecture documents are kept in
[`docs/es`](docs/es/).

## Documentation

- [Architecture](ARCHITECTURE.md)
- [HTTP and gRPC API](API.md)
- [Configuration](CONFIGURATION.md)
- [Persistence and schema management](PERSISTENCE.md)
- [Production cache](CACHE.md)
- [Security model](SECURITY.md)
- [Testing and troubleshooting](TESTING.md)
- [Local Docker deployment](DEPLOYMENT.md)
- [Release and rollback](RELEASE.md)
- [Dependency provenance and updates](docs/DEPENDENCIES.md)
- [Architecture decision records](docs/adr/README.md)
- [Improvement plan](IMPROVEMENT_PLAN.md)

## Components

| Area | Implementation |
| --- | --- |
| Persistence | MongoDB through `mongo-cxx-driver`, with migrations, validators, indexes, CRUD, filtering, and aggregation |
| APIs | cpp-httplib HTTP server and gRPC/Protocol Buffers service |
| Cache | Thread-safe, typed LRU post cache; educational LRU, LFU, and random-replacement implementations |
| Security | Role-based Bearer tokens, PBKDF2 password hashing, optional TLS, CORS, security headers, and rate limiting |
| Operations | Structured JSON logs, request IDs, health/readiness endpoints, and Prometheus metrics |
| Delivery | CMake, GoogleTest, Docker Compose, GitHub Actions, GHCR, and ECS Fargate templates |

## Repository Layout

```text
.
├── proto/                 # gRPC contract
├── src/
│   ├── api/               # gRPC service, server, and sample client
│   ├── auth/              # authorization, rate limiting, and password hashing
│   ├── cache/             # production and educational caches
│   ├── common/            # logging, metrics, and request context
│   ├── config/            # centralized runtime configuration
│   ├── db/                # MongoDB repository and store abstraction
│   ├── model/             # domain models and validation
│   └── server/            # HTTP routes and server
├── tests/                 # unit and MongoDB-backed integration tests
├── deploy/                # Nginx and AWS ECS definitions
├── docs/                  # ADRs, dependencies, and Spanish translations
└── scripts/               # setup, MongoDB, dependency, and smoke-test helpers
```

## Prerequisites

- CMake 3.16 or newer
- A C++17 compiler
- OpenSSL
- gRPC with `grpc_cpp_plugin`
- Protocol Buffers
- MongoDB C++ driver 4.x
- GoogleTest when `BUILD_TESTS=ON`
- MongoDB for integration tests and local execution

On macOS with Homebrew:

```bash
brew tap mongodb/brew
brew install cmake openssl@3 grpc protobuf mongo-cxx-driver googletest \
  mongodb-community@8.0
```

## Build and Test

The macOS helper installs missing Homebrew dependencies, including MongoDB Community
Edition as a native host installation, starts that local MongoDB, builds the project,
and runs all tests:

```bash
./scripts/setup.sh
```

Do not run `setup.sh` if MongoDB should remain Docker-only. Use the Docker Compose
smoke test documented below; it runs MongoDB inside an isolated container and does not
require a native `mongod` installation.

Or run the steps manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/grpc;/opt/homebrew/opt/mongo-cxx-driver;/opt/homebrew/opt/bsoncxx;/opt/homebrew/opt/mongo-c-driver;/opt/homebrew/opt/googletest"
cmake --build build -j4
ctest --test-dir build --output-on-failure --timeout 20
```

If the shell is running under Rosetta 2 on Apple Silicon, prefix the CMake configure
and build commands with `arch -arm64` to avoid mixed-architecture dependencies.

## Run Locally

```bash
./scripts/start_mongo.sh
./build/blog_http_server 0.0.0.0 8080
./build/blog_grpc_server 0.0.0.0:50051
```

The servers default to port `5000` for HTTP and `50051` for gRPC when no positional
arguments or environment overrides are supplied. The commands above explicitly use
the development ports documented in this repository.

Create and retrieve a post while local authentication is disabled:

```bash
post_id="$(curl --fail --silent --show-error \
  -H 'Content-Type: application/json' \
  -d '{"title":"Hello C++","author":"writer","content":"First post","tags":["cpp"]}' \
  http://127.0.0.1:8080/posts | sed -E 's/.*"id":"([^"]+)".*/\1/')"
curl --fail --silent --show-error "http://127.0.0.1:8080/posts/${post_id}"
```

For an authenticated run, copy `.env.example`, provide unique tokens of at least 32
characters, export the values, and set `AUTH_REQUIRED=true`. Never commit `.env` or
real credentials. See [Configuration](CONFIGURATION.md) and [Security](SECURITY.md).

## Docker Compose

```bash
docker compose up --build --detach --wait
curl --fail http://localhost:8080/ready
./scripts/smoke_test.sh
docker compose down
```

Compose exposes HTTP directly at `http://localhost:8080`, through Nginx at
`http://localhost`, and gRPC at `localhost:50051`. MongoDB and the gRPC metrics port
remain private to the Compose network.

## API Summary

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/health` | Process liveness |
| `GET` | `/ready` | MongoDB-aware readiness |
| `GET` | `/metrics` | Prometheus metrics |
| `GET` | `/posts` | Filtered, paginated post list |
| `POST` | `/posts` | Create a post |
| `GET` | `/posts/:id` | Read a post |
| `PUT` | `/posts/:id` | Replace a post |
| `DELETE` | `/posts/:id` | Delete a post |
| `GET` | `/stats/posts-per-author` | Count posts grouped by author |

The complete HTTP payloads, gRPC methods, authentication metadata, and error mapping
are documented in [API.md](API.md).

## CI/CD and Deployment

The `C++ CI` workflow runs formatting, strict builds, unit and integration tests,
sanitizers, static analysis, coverage, security scans, and a container smoke test.
After a successful push build, the publication workflow scans and publishes immutable
`sha-<commit>` images to GHCR with an SBOM and provenance attestation.

AWS deployment templates target separate HTTP and gRPC services on ECS Fargate. They
are templates only: no live AWS environment is provisioned by this repository. See
[Release and Rollback](RELEASE.md) before deploying.

## Project Origin

The book presents the backend as chapter-level snippets rather than one integrated
application. This repository retains the core class, protocol, and operation ideas
from chapters 5–11 while adding the production boundaries and verification required
to run them together.
