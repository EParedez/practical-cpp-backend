# ADR-0002: MongoDB Repository and Startup Migrations

## Status

Accepted

## Context

Both transports need identical persistence behavior, and schema validation and indexes
must exist before traffic reaches an instance.

## Decision

Keep direct MongoDB driver usage inside `BlogRepository`, accessed through
`BlogStore`. Run idempotent, versioned migrations, validators, index creation, and
verification during application startup.

## Consequences

Transport tests can use substitute stores, and an instance fails before serving
traffic when its schema cannot be prepared. Startup requires database availability,
and future migrations must remain compatible with rolling deployment and rollback.
