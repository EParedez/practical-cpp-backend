# ADR-0005: ECS Fargate Deployment Model

## Status

Accepted

## Context

The previous Elastic Beanstalk artifacts targeted an unrelated Go/WSGI shape and did
not represent the two C++ server processes or their container workflow.

## Decision

Use one immutable container image in two ECS Fargate services: one HTTP task and one
gRPC task. Inject secrets through AWS Secrets Manager, use managed MongoDB on private
networking, and terminate TLS at suitable load balancers or in the application.

## Consequences

The deployment model matches the built artifacts and permits independent scaling.
Operators must provision networking, load balancers, MongoDB, secrets, and concrete
template values. The checked-in task definitions are templates and do not establish
that a live AWS deployment has been performed.
