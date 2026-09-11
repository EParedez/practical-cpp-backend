# Security Model

The application supports role-based Bearer-token authentication for both HTTP and
gRPC. It is intentionally disabled in the local Compose defaults so the example can
be started without provisioning secrets. Production deployments must set
`AUTH_REQUIRED=true` and inject tokens from a secret manager.

## Roles and authorization

Roles form the hierarchy `reader < writer < admin`. `POST`, `PUT`, and `DELETE`
operations below `/posts` require `writer` or `admin`. `/metrics` requires
at least `reader` whenever authentication is enabled. Post and statistics reads are
public unless `PROTECT_READ_ENDPOINTS=true`.

HTTP clients send `Authorization: Bearer <token>`. gRPC clients send the same value
in `authorization` metadata. Missing or invalid credentials return HTTP `401` or
gRPC `UNAUTHENTICATED`; insufficient roles return HTTP `403` or gRPC
`PERMISSION_DENIED`. Tokens are compared in constant time and are never logged.

Generate independent random values of at least 32 characters for each configured
role. Rotate a token by deploying a new secret, updating clients, and then removing
the old deployment. The current configuration accepts one token per role, so a
zero-downtime rotation requires a rolling deployment or a secret-provider extension.

## Password handling

`CreateUser` accepts a plaintext password at the repository boundary and immediately
stores a salted PBKDF2-HMAC-SHA-256 representation with 210,000 iterations. User read
models never contain the stored password hash. This repository does not expose user
registration or password login endpoints; a future identity API should use the same
component and add password policy, reset, lockout, and breach checks.

## Transport and browser controls

Set both `TLS_CERTIFICATE_FILE` and `TLS_PRIVATE_KEY_FILE` to enable TLS directly in
the HTTP and gRPC servers. A production load balancer may terminate TLS instead, but
unencrypted listeners must then remain on private networks. Never mount private keys
into an image layer.

CORS is deny-by-default. Set `CORS_ALLOWED_ORIGIN` to one exact trusted origin when a
browser client is needed; `*` is supported only for intentionally public APIs. HTTP
responses include a restrictive Content Security Policy, `nosniff`, no-referrer,
and disabled camera, microphone, and geolocation permissions.

Authenticated operations are rate limited per token role identity. This in-process
fixed-window limit is not shared across replicas; production deployments needing a
global quota should enforce it at the gateway or use a shared limiter.

## Operational checklist

- Store MongoDB credentials and Bearer tokens in the deployment secret manager.
- Terminate TLS at the application or a trusted private load balancer.
- Keep `/metrics` and the gRPC metrics listener private.
- Run dependency review, static analysis, sanitizer tests, SBOM generation, image
  scanning, and secret scanning in CI.
- Treat logs and request IDs as operational data; do not add request bodies or
  authorization metadata to logs.
