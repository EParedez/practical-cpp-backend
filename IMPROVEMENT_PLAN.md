# Practical C++ Backend Improvement Plan

## Purpose

This document turns the current technical review into an incremental delivery plan.
The goal is to improve correctness, deployability, testability, security, and
maintainability without attempting a large rewrite.

The existing `README.md` and `ARCHITECTURE.md` are intentionally preserved. New
documentation should be written in English. Existing Spanish documentation can be
translated during the documentation phase after its technical claims have been
verified.

## Current Baseline

The project currently provides:

- A C++17 backend with HTTP and gRPC interfaces.
- MongoDB persistence through `mongo-cxx-driver`.
- LRU, LFU, and random-replacement cache implementations.
- Google Test unit and integration tests.
- CMake, Docker, Docker Compose, Nginx, GitHub Actions, and AWS deployment files.

The project builds successfully in the current development environment. The current
suite contains 29 unit tests and 14 MongoDB-backed integration tests; all 43 pass.
Integration tests require a reachable MongoDB instance and use bounded connection and
RPC deadlines.

## Delivery Principles

- Keep every phase small enough to review and merge independently.
- Fix correctness and local reproducibility before adding features.
- Add or update tests with every behavior change.
- Keep transport concerns separate from persistence and domain logic.
- Do not report operational failures as valid empty or not-found responses.
- Prefer explicit, typed configuration and errors over implicit defaults.
- Write all new documentation, comments, operational instructions, and public API
  descriptions in English.
- Preserve existing Spanish documentation until an English replacement has been
  reviewed for accuracy.

## Phase 0: Baseline and Safety Net

**Status:** In progress. The test split, deadlines, and Phase 1/2 regression coverage
are complete; cache edge cases, compiler warnings, sanitizers, and dynamic gRPC test
ports remain.

### Objective

Create a reliable baseline so later phases can distinguish new regressions from
existing behavior.

### Work Items

- [x] Add CTest labels for unit and integration tests.
- [x] Add commands for running only unit tests and only integration tests.
- [x] Add finite MongoDB server-selection and connection timeouts for tests.
- [x] Add gRPC client deadlines in integration tests.
- [x] Use `localhost` as the test client destination instead of `0.0.0.0`.
- [ ] Avoid a fixed integration-test port, or allocate a port safely.
- [ ] Add regression tests for:
  - [x] Invalid MongoDB ObjectIds.
  - [x] Idempotent updates.
  - [x] JSON strings containing quotes, newlines, and Unicode.
  - [ ] Zero and negative cache capacities.
  - [ ] LFU values equal to `-1`.
- [ ] Add compiler warnings for project-owned code.
- [ ] Add AddressSanitizer and UndefinedBehaviorSanitizer options for local and CI
  builds.

### Acceptance Criteria

- Unit tests run without MongoDB.
- Integration tests fail within a documented, short timeout when MongoDB is absent.
- Existing behavior is covered before production code is refactored.
- CI has separate, clearly named unit and integration test steps.

### Suggested Deliverables

1. Test labeling and timeout pull request.
2. Regression-test pull request.
3. Warning and sanitizer pull request.

## Phase 1: Correctness and Error Semantics

**Status:** Complete as of 2026-09-10.

### Objective

Ensure the APIs return accurate results for invalid input, missing resources, and
infrastructure failures.

### Work Items

- [x] Introduce a repository result/error type that distinguishes:
  - [x] Success.
  - [x] Not found.
  - [x] Invalid input or invalid ObjectId.
  - [x] Duplicate key or conflict.
  - [x] Database unavailable or internal database failure.
- [x] Stop converting all MongoDB exceptions into `false`, `nullopt`, or empty lists.
- [x] Map repository errors consistently:
  - [x] HTTP: `400`, `404`, `409`, and `500`/`503`.
  - [x] gRPC: `INVALID_ARGUMENT`, `NOT_FOUND`, `ALREADY_EXISTS`, and
    `INTERNAL`/`UNAVAILABLE`.
- [x] Validate ObjectIds before constructing `bsoncxx::oid` values.
- [x] Use `matched_count()` rather than `modified_count()` to determine whether an
  update target exists.
- [x] Decide whether `author` and `published_date` are mutable, then make the proto,
  service, repository, and tests consistent.
- [x] Return every supported post field from `GetAllPosts`.
- [x] Validate required fields and define maximum lengths for titles, authors,
  content, tags, and identifiers.

### Acceptance Criteria

- A malformed ID never terminates a request handler or server process.
- A database outage is never returned as `404` or as a successful empty collection.
- Repeating an update with identical data succeeds when the record exists.
- HTTP and gRPC expose equivalent domain behavior.

## Phase 2: HTTP API and Application Structure

**Status:** Complete as of 2026-09-10.

### Objective

Make the HTTP API standards-compliant, safe to serialize, and directly testable.

### Work Items

- [x] Replace manual JSON concatenation with maintained BSON/JSON serialization.
- [x] Accept `application/json` request bodies for post creation and updates.
- [x] Return a consistent error envelope with an error code, message, and request ID.
- [x] Add `PUT` or `PATCH /posts/:id`.
- [x] Define consistent response fields for list and detail endpoints.
- [x] Add pagination to `GET /posts` with bounded defaults and limits.
- [x] Extract route registration and application construction from `main()`.
- [x] Add HTTP handler tests that do not require launching an external process.
- [x] Add request body, header, and timeout limits appropriate for the API.
- [x] Add a database-aware readiness endpoint while keeping `/health` as a lightweight
  liveness endpoint.

### Acceptance Criteria

- All responses are valid JSON for arbitrary valid user input.
- HTTP handlers can be tested in-process.
- List endpoints cannot load an unbounded collection into memory.
- Liveness and readiness have distinct, documented behavior.

## Phase 3: Configuration, Observability, and Graceful Operation

**Status:** Complete as of 2026-09-10. Cache counters are exposed now and will become
active when production cache reads are integrated in Phase 6.

### Objective

Make runtime behavior configurable and diagnosable across local, container, and cloud
environments.

### Work Items

- [x] Add a centralized configuration type.
- [x] Read `MONGODB_URI`, database name, HTTP host/port, gRPC address, cache capacity,
  and timeout values from environment variables.
- [x] Keep command-line arguments only as documented overrides, if needed.
- [x] Validate configuration at startup and fail with actionable messages.
- [x] Add structured logging with severity, timestamp, request ID, operation, status,
  and duration.
- [x] Do not log passwords, authorization tokens, or full sensitive request bodies.
- [x] Add graceful shutdown for HTTP and gRPC servers.
- [x] Add basic metrics for request counts, latency, errors, MongoDB operations, and
  cache hits/misses.

### Acceptance Criteria

- The same binaries run locally and in containers without rebuilding.
- Invalid configuration fails before the server starts accepting traffic.
- Every failed request can be correlated with a structured log entry.
- Shutdown stops accepting new work and completes or cancels in-flight work safely.

## Phase 4: Docker and Local Deployment

**Status:** Complete as of 2026-09-10.

### Objective

Provide a reproducible, working local deployment using Docker Compose.

### Work Items

- [x] Allow MongoDB to listen on the Compose network while avoiding unnecessary host
  exposure.
- [x] Change Nginx upstreams from `localhost` to the Compose service name
  `blog_http:8080`.
- [x] Add MongoDB, HTTP, and gRPC health checks.
- [x] Use health-based dependency conditions or application-level retry/backoff.
- [x] Remove the obsolete Compose `version` field.
- [x] Avoid building the same application image separately for each service.
- [x] Pin base images and important dependencies to reviewed versions or digests.
- [x] Run application containers as a non-root user.
- [x] Add a `.dockerignore` file.
- [x] Verify runtime library packaging and keep the final image minimal.
- [x] Add a smoke test that starts Compose and exercises health, create, read, update,
  list, and delete operations.

### Acceptance Criteria

- `docker compose up --build` reaches a healthy state without manual intervention.
- Nginx can reach the HTTP service, and both application services can reach MongoDB.
- The smoke test passes from a clean machine with Docker as the only prerequisite.
- Application containers do not run as root.

## Phase 5: Persistence Quality and Performance

### Objective

Make MongoDB access predictable as data volume and concurrency increase.

### Work Items

- [ ] Create and verify indexes for unique usernames and common post queries.
- [ ] Store timestamps as BSON dates instead of unconstrained strings.
- [ ] Define a schema-validation strategy for users and posts.
- [ ] Add deterministic sorting and cursor- or page-based pagination.
- [ ] Add query filters for author, tags, and publication date as required.
- [ ] Configure pool size and MongoDB timeouts through runtime configuration.
- [ ] Define whether comments are embedded or separated based on expected growth.
- [ ] Add database migration or startup-index management with clear versioning.
- [ ] Add integration tests for duplicate users, malformed documents, indexes, and
  pagination boundaries.

### Acceptance Criteria

- Common queries use documented indexes.
- Date filtering and ordering use native date types.
- Large collections are never returned in a single unbounded response.
- Schema and index changes are reproducible in every environment.

## Phase 6: Production Cache Integration

### Objective

Replace the demonstration-only integer caches with a cache suitable for blog reads.

### Work Items

- [ ] Decide whether the educational LRU/LFU/RR implementations remain examples or
  become production components.
- [ ] Introduce a cache interface using post IDs as keys and post DTOs as values.
- [ ] Remove `-1` sentinel semantics in favor of `std::optional` or a result type.
- [ ] Reject or safely support zero and negative capacities.
- [ ] Make the selected cache safe for concurrent HTTP and gRPC handlers.
- [ ] Integrate cache lookup into `GetPost`.
- [ ] Invalidate or update entries after post update and deletion.
- [ ] Define TTL, stale-data behavior, and maximum memory usage.
- [ ] Expose hit, miss, eviction, and invalidation metrics.
- [ ] Correct the documented RR complexity or change its data structures to meet the
  intended complexity.

### Acceptance Criteria

- Cached and uncached reads return equivalent data.
- Updates and deletes cannot leave stale entries beyond the documented policy.
- Concurrency and eviction behavior are covered by tests and sanitizers.
- Cache effectiveness is measurable.

## Phase 7: Security

### Objective

Make the security claims in the documentation true and testable.

### Work Items

- [ ] Define authentication and authorization requirements before choosing JWT,
  OAuth/OIDC, or another mechanism.
- [ ] Protect create, update, and delete operations in both HTTP and gRPC.
- [ ] Introduce roles such as reader, writer, and administrator if required.
- [ ] Hash passwords in a dedicated authentication component using an appropriate
  password-hashing algorithm; never trust callers to submit a pre-hashed password.
- [ ] Stop returning password hashes from repository read models.
- [ ] Add TLS for external HTTP and gRPC traffic.
- [ ] Configure CORS explicitly if browser clients are expected.
- [ ] Apply rate limits to real sensitive endpoints, not only example routes.
- [ ] Replace obsolete security headers and add a suitable Content Security Policy
  where relevant.
- [ ] Add dependency, container, and secret scanning to CI.
- [ ] Add authorization and abuse-case tests.

### Acceptance Criteria

- Anonymous callers cannot mutate protected resources.
- Credentials and password hashes never appear in responses or logs.
- External production traffic is encrypted.
- Security behavior is enforced by automated tests.

## Phase 8: CI/CD and Release Engineering

### Objective

Ensure every released artifact is tested, traceable, and reproducible.

### Work Items

- [ ] Make Docker publication depend on successful build, unit, integration, and smoke
  tests.
- [ ] Build on pull requests without pushing images.
- [ ] Publish immutable commit/version tags in addition to any moving tag.
- [ ] Add dependency caching where it does not weaken reproducibility.
- [ ] Add formatting, static analysis, sanitizer, and coverage jobs.
- [ ] Generate a software bill of materials and scan the final image.
- [ ] Add CMake install rules and package only required runtime artifacts.
- [ ] Choose one supported deployment model for AWS, preferably a container-based
  model for the current architecture.
- [ ] Remove or replace the current Go/WSGI Elastic Beanstalk configuration.
- [ ] Document rollback and database compatibility procedures.

### Acceptance Criteria

- A failed test cannot publish a release image.
- Every deployed image maps to a source commit and immutable tag.
- The deployment process works from documented commands without manually copying
  build products.
- Rollback steps are tested and documented.

## Phase 9: Documentation Alignment

### Objective

Provide accurate English documentation that reflects the implemented system.

### Work Items

- [ ] Translate `README.md` into English after verifying every setup and deployment
  command.
- [ ] Translate and update `ARCHITECTURE.md` after the application boundaries stabilize.
- [ ] Keep the original Spanish files or preserve them through clearly named localized
  copies until the English versions are accepted.
- [ ] Mark planned features as planned rather than implemented.
- [ ] Document HTTP and gRPC contracts with request and response examples.
- [x] Document configuration variables, defaults, constraints, and precedence.
- [ ] Document local development, tests, Docker Compose, troubleshooting, security,
  and deployment.
- [ ] Record important architectural decisions in short ADRs under `docs/adr/`.
- [ ] Add dependency provenance and an update procedure for the vendored
  `cpp-httplib` header.

### Acceptance Criteria

- All active documentation is available in English.
- Setup instructions have been verified on a clean environment.
- Documentation does not claim that unfinished security, cache, or deployment features
  are complete.
- Major architectural decisions have an owner, rationale, and consequences.

## Recommended Phase Order

The default sequence is:

1. Phase 0 — establish the safety net.
2. Phase 1 — correct error and update behavior.
3. Phase 2 — repair and test the HTTP boundary.
4. Phase 3 — centralize configuration and observability.
5. Phase 4 — make the container stack reproducible.
6. Phase 5 — improve persistence behavior and performance.
7. Phase 6 — integrate a real cache.
8. Phase 7 — implement the agreed security model.
9. Phase 8 — harden releases and deployment.
10. Phase 9 — finalize and verify the English documentation set.

Phase 9 documentation updates should also happen incrementally inside every earlier
phase; the final documentation phase is for consolidation and removal of stale claims.

## Definition of Done for Every Phase

A phase is complete only when:

- [ ] Its acceptance criteria are met.
- [ ] New and changed behavior has automated tests.
- [ ] Unit and applicable integration tests pass.
- [ ] Sanitizer and static-analysis results have been reviewed.
- [ ] Configuration and operational changes are documented in English.
- [ ] No credentials, machine-specific paths, or generated build artifacts are added
  to version control.
- [ ] The change can be reviewed and rolled back independently.

## Suggested First Iteration

The first implementation iteration should remain deliberately small:

1. Label unit and integration tests and add finite timeouts.
2. Add regression tests for malformed IDs and idempotent updates.
3. Introduce typed repository errors.
4. Correct HTTP and gRPC error mapping.
5. Fix the Docker Compose MongoDB and Nginx networking configuration.
6. Add a Compose smoke test.

Completing this iteration removes the largest correctness and reproducibility risks
without requiring authentication, caching, or a broader architectural rewrite.
