# ADR-0004: Role-Based Static Bearer Tokens

## Status

Accepted

## Context

The sample backend needs enforceable service authorization without introducing a full
identity platform. Local development must remain easy to start.

## Decision

Use independently configured Bearer tokens for `reader`, `writer`, and `admin` roles.
Disable authentication by default locally, require it in production, and apply the
same hierarchy to HTTP and gRPC. Keep token values in environment-backed secret
management and never log them.

## Consequences

Authorization is small and testable, but token identity is role-wide, rotation is
limited to one token per role per deployment, and the in-process limiter cannot impose
a global multi-replica quota. User-facing identity remains future work.
