# Local Docker Deployment

Docker Compose provides a reproducible local stack with MongoDB, the HTTP server, the
gRPC server, and Nginx. Docker is the only prerequisite for this workflow.

## Start the Stack

```bash
docker compose up --build --detach --wait
docker compose ps
```

The exposed services are:

| Service | Address | Purpose |
| --- | --- | --- |
| Nginx | `http://localhost/` | Reverse proxy to the HTTP API |
| HTTP | `http://localhost:8080/` | Direct HTTP API and metrics |
| gRPC | `localhost:50051` | Blog gRPC API |

MongoDB is reachable only from the Compose network at `mongo:27017`; it is not
published to the host. Both application services use the same locally tagged image,
`practical-cpp-backend:local`.

## Health and Observability

Compose waits for MongoDB to respond to a database ping before starting the
application services. The HTTP health check uses `/ready`, the gRPC check uses the
internal metrics listener at `http://blog_grpc:9090/health`, and Nginx starts after
HTTP is healthy. Port 9090 is not published to the host.

Before opening their listeners, both application processes apply the idempotent
database migration and verify collection validators and indexes. See
[Persistence and Schema Management](PERSISTENCE.md).

```bash
curl --fail http://localhost:8080/health
curl --fail http://localhost:8080/ready
curl --fail http://localhost:8080/metrics
docker compose logs --follow blog_http blog_grpc
```

To inspect process-local gRPC counters from inside its container:

```bash
docker compose exec blog_grpc curl --fail http://127.0.0.1:9090/metrics
```

Application logs are newline-delimited JSON and include request IDs, operations,
statuses, and durations. HTTP clients may supply `X-Request-ID`; otherwise the server
generates one.

Production cache policy and process-local consistency behavior are documented in
[Production Post Cache](CACHE.md).

## Smoke Test

The smoke test creates an isolated Compose project, waits for every service, exercises
the HTTP create/read/update/list/delete flow, and removes its containers, network, and
MongoDB volume afterward.

```bash
./scripts/smoke_test.sh
```

Set `KEEP_SMOKE_STACK=1` to keep the isolated stack for troubleshooting:

```bash
KEEP_SMOKE_STACK=1 ./scripts/smoke_test.sh
docker compose --project-name practicalcppbackend-smoke logs
docker compose --project-name practicalcppbackend-smoke down --volumes --remove-orphans
```

## Stop the Development Stack

```bash
docker compose down
```

Add `--volumes` only when the local MongoDB data should also be deleted:

```bash
docker compose down --volumes
```

## Image Design

The multi-stage Dockerfile builds the MongoDB C++ driver and application binaries in
the builder stage, then copies only the runtime artifacts into Ubuntu 24.04. The
MongoDB C++ driver is pinned to 4.5.0, while Compose pins MongoDB to 8.0 and Nginx to
1.27. The runtime stage derives release-specific library package names from stable
Ubuntu development-package metadata, but installs only runtime libraries. CI builds
the image for both `linux/amd64` and `linux/arm64`. Application containers run as
UID/GID 10001 instead of root. `.dockerignore` excludes repository metadata, local
agent state, build output, and logs.

## Production deployment

Local Compose disables authentication by default. Before exposing the service, enable
authentication, inject unique role tokens from a secret manager, and terminate TLS as
described in [Security Model](SECURITY.md). The supported AWS release target and
tested rollback procedure are documented in [Release and Rollback](RELEASE.md).
