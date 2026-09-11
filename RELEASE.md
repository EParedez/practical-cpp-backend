# Release and Rollback

## Release gates

Pull requests build the application and container without publishing. Unit,
integration, sanitizer, static-analysis, coverage, dependency-review, and Compose
smoke jobs run in the `C++ CI` workflow. A push to `main` or `master` can publish only
after that workflow succeeds.

The publication workflow checks out the tested commit, generates an SPDX SBOM, and
blocks on high or critical vulnerability findings before it publishes to GHCR with
both `latest` and an immutable `sha-<commit>` tag. It then creates a GitHub
build-provenance attestation. Release and deployment records must use the digest,
not `latest`.

## AWS deployment model

The supported AWS target is ECS Fargate behind load balancers, with MongoDB supplied
by a managed provider reachable on private networking. HTTP and gRPC run as separate
ECS services from the same immutable image. Templates live in `deploy/aws`; replace
every `REPLACE_WITH_...` value and store all credentials in AWS Secrets Manager.

Register and deploy a tested task definition:

```bash
aws ecs register-task-definition \
  --cli-input-json file://deploy/aws/ecs-http-task-definition.json
aws ecs update-service --cluster "$ECS_CLUSTER" --service "$HTTP_SERVICE" \
  --task-definition practical-cpp-backend-http --force-new-deployment
aws ecs wait services-stable --cluster "$ECS_CLUSTER" --services "$HTTP_SERVICE"
```

Use an HTTPS Application Load Balancer for HTTP. Use an appropriate TLS-enabled
Network or Application Load Balancer for gRPC, depending on the platform features
selected. Keep tasks and MongoDB off public subnets whenever possible.

## Rollback

1. Identify the last healthy image digest and task-definition revision.
2. Confirm its database compatibility in `PERSISTENCE.md`; migrations must remain
   backward-compatible for at least one deployed application revision.
3. Update the ECS service to the previous task-definition revision.
4. Wait for service stability and verify `/health`, `/ready`, one authenticated
   mutation, one read, and relevant metrics.
5. Record the failed digest, previous digest, migration version, and reason.

Do not roll back database data automatically. If a migration requires an irreversible
change, deploy an expand/migrate/contract sequence across separate releases.
