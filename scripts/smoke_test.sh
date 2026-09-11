#!/usr/bin/env bash
set -euo pipefail

PROJECT_NAME="practicalcppbackend-smoke"
export AUTH_REQUIRED=true
export BLOG_WRITER_TOKEN="${BLOG_WRITER_TOKEN:-smoke-writer-token-with-32-characters}"

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
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  http://127.0.0.1:8080/metrics >/dev/null
compose exec -T blog_grpc curl --fail --silent \
  http://127.0.0.1:9090/metrics >/dev/null

anonymous_status="$(compose exec -T blog_http curl --silent --output /dev/null \
  --write-out '%{http_code}' -H 'Content-Type: application/json' \
  -d '{"title":"blocked","author":"anonymous"}' \
  http://127.0.0.1:8080/posts)"
if [[ "$anonymous_status" != "401" ]]; then
  echo "Expected anonymous mutation to return 401, got $anonymous_status." >&2
  exit 1
fi

created_response="$(compose exec -T blog_http curl --fail --silent \
  -H 'Content-Type: application/json' \
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  -d '{"title":"Smoke test","author":"smoke","content":"container check","tags":["smoke"]}' \
  http://127.0.0.1:8080/posts)"
post_id="$(printf '%s' "$created_response" | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"

if [[ -z "$post_id" ]]; then
  echo "Smoke test could not extract the created post ID." >&2
  exit 1
fi

compose exec -T blog_http curl --fail --silent \
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null
compose exec -T blog_http curl --fail --silent \
  -X PUT -H 'Content-Type: application/json' \
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  -d '{"title":"Updated smoke test","author":"smoke","content":"updated","tags":["smoke"]}' \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null
compose exec -T blog_http curl --fail --silent \
  'http://127.0.0.1:8080/posts?limit=10&offset=0' >/dev/null
compose exec -T blog_http curl --fail --silent \
  'http://127.0.0.1:8080/posts?author=smoke&tag=smoke&published_from=2020-01-01T00%3A00%3A00Z' >/dev/null
compose exec -T blog_http curl --fail --silent -X DELETE \
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  "http://127.0.0.1:8080/posts/$post_id" >/dev/null

metrics="$(compose exec -T blog_http curl --fail --silent \
  -H "Authorization: Bearer $BLOG_WRITER_TOKEN" \
  http://127.0.0.1:8080/metrics)"
grep -Eq 'blog_cache_hits_total [1-9][0-9]*' <<<"$metrics"
grep -Eq 'blog_cache_invalidations_total [1-9][0-9]*' <<<"$metrics"

echo "Docker Compose smoke test passed."
