# Architecture Decision Records

ADRs capture decisions that materially shape the application. Their status describes
the decision, not whether every possible future extension has been implemented.

| ADR | Decision | Status |
| --- | --- | --- |
| [0001](0001-separate-http-and-grpc-processes.md) | Separate HTTP and gRPC processes | Accepted |
| [0002](0002-mongodb-repository-and-startup-migrations.md) | MongoDB repository and startup migrations | Accepted |
| [0003](0003-process-local-typed-lru-cache.md) | Process-local typed LRU cache | Accepted |
| [0004](0004-role-based-static-bearer-tokens.md) | Role-based static Bearer tokens | Accepted |
| [0005](0005-ecs-fargate-deployment-model.md) | ECS Fargate deployment model | Accepted |

New records use the next four-digit number and contain Context, Decision,
Consequences, and Status sections. Accepted records are immutable except for typo or
link corrections; supersede a changed decision with a new ADR.
