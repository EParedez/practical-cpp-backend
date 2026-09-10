# Configuration Reference

The HTTP and gRPC servers use the same centralized runtime configuration. Values are
loaded from environment variables, then optional positional command-line arguments
override the corresponding environment values. Invalid configuration is reported as
a structured fatal log before the server accepts traffic.

## Environment Variables

| Variable | Default | Valid values | Used by |
| --- | --- | --- | --- |
| `MONGODB_URI` | `mongodb://localhost:27017` | Non-empty MongoDB connection URI | HTTP and gRPC |
| `BLOG_DATABASE` | `blog` | Non-empty database name | HTTP and gRPC |
| `HTTP_HOST` | `0.0.0.0` | Non-empty bind host | HTTP |
| `PORT` | `5000` | Integer from 1 to 65535 | HTTP |
| `GRPC_ADDRESS` | `0.0.0.0:50051` | Non-empty address containing a colon | gRPC |
| `GRPC_METRICS_HOST` | `0.0.0.0` | Non-empty bind host | gRPC metrics |
| `GRPC_METRICS_PORT` | `9090` | Integer from 1 to 65535 | gRPC metrics |
| `CACHE_CAPACITY` | `128` | Integer from 0 to 1,000,000 | gRPC |
| `HTTP_READ_TIMEOUT_SECONDS` | `5` | Integer from 1 to 3600 | HTTP |
| `HTTP_WRITE_TIMEOUT_SECONDS` | `5` | Integer from 1 to 3600 | HTTP |
| `HTTP_KEEP_ALIVE_TIMEOUT_SECONDS` | `10` | Integer from 1 to 3600 | HTTP |
| `HTTP_KEEP_ALIVE_MAX_COUNT` | `20` | Integer from 1 to 10,000 | HTTP |
| `SHUTDOWN_GRACE_SECONDS` | `10` | Integer from 1 to 300 | HTTP and gRPC |

`CACHE_CAPACITY` configures the existing gRPC cache allocation. Cache-backed reads
and active hit/miss accounting remain planned for Phase 6.

## Command-Line Overrides

The positional arguments are retained for local compatibility:

```bash
./build/blog_http_server [host] [port] [mongodb_uri]
./build/blog_grpc_server [address] [mongodb_uri]
```

For example, the following uses the environment database name and overrides the HTTP
host and port:

```bash
BLOG_DATABASE=development ./build/blog_http_server 127.0.0.1 8080
```

The precedence order is: built-in default, environment variable, command-line
override.

## Logging and Secrets

Logs are emitted as one JSON object per line. Startup logs include bind addresses and
the database name, but never include the MongoDB URI. Do not place credentials or
tokens in command-line arguments because they may be visible to local process-inspection
tools. Use environment or secret-management facilities for sensitive values.
