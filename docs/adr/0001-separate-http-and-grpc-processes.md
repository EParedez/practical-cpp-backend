# ADR-0001: Separate HTTP and gRPC Processes

## Status

Accepted

## Context

Browser-facing HTTP and service-facing gRPC need different transport configuration,
health checks, scaling characteristics, and load balancers. They share the same blog
domain and MongoDB schema.

## Decision

Build HTTP and gRPC as separate executables over the shared `BlogStore` abstraction
and `BlogRepository` implementation. Each process owns its lifecycle, connection pool,
cache, security components, and metrics.

## Consequences

The services can be deployed and scaled independently and transport concerns remain
isolated. Mutations do not synchronously invalidate another process's local cache, and
operational dashboards must aggregate metrics from both processes.
