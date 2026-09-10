#!/usr/bin/env bash
set -euo pipefail

PROJECT_NAME="practicalcppbackend-smoke"

compose() {
  docker compose --project-name "$PROJECT_NAME" "$@"
}

cleanup() {
  if [[ "${KEEP_SMOKE_STACK:-0}" != "1" ]]; then
    compose down --volumes --remove-orphans
  fi
}

trap cleanup EXIT

compose up --build --detach --wait

compose exec -T blog_http curl --fail --silent \
  http://127.0.0.1:8080/health >/dev/null
compose exec -T blog_http curl --fail --silent \
  http://127.0.0.1:8080/ready >/dev/null
compose exec -T blog_http curl --fail --silent \
  http://127.0.0.1:8080/metrics >/dev/null
compose exec -T blog_grpc curl --fail --silent \
  http://127.0.0.1:9090/metrics >/dev/null

created_response="$(compose exec -T blog_http curl --fail --silent \
  -H 'Content-Type: application/json' \
  -d '{"title":"Smoke test","author":"smoke","content":"container check","tags":["smoke"]}' \
  http://127.0.0.1:8080/posts)"
post_id="$(printf '%s' "$created_response" | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"

if [[ -z "$post_id" ]]; then
  echo "Smoke test could not extract the created post ID." >&2
  exit 1
fi

compose exec -T blog_http curl --fail --silent \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null
compose exec -T blog_http curl --fail --silent \
  -X PUT -H 'Content-Type: application/json' \
  -d '{"title":"Updated smoke test","author":"smoke","content":"updated","tags":["smoke"]}' \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null
compose exec -T blog_http curl --fail --silent \
  'http://127.0.0.1:8080/posts?limit=10&offset=0' >/dev/null
compose exec -T blog_http curl --fail --silent -X DELETE \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null

echo "Docker Compose smoke test passed."
