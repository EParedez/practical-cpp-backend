# HTTP API

The HTTP API accepts and returns UTF-8 JSON. Error responses use a consistent
envelope and include an `X-Request-ID` response header.

Mutation endpoints require a `writer` or `admin` Bearer token when authentication is
enabled. Send `Authorization: Bearer <token>` over HTTP or the equivalent lowercase
`authorization` metadata over gRPC. See [Security Model](SECURITY.md).

For example, an authenticated HTTP mutation uses:

```bash
curl --fail --silent --show-error \
  -H "Authorization: Bearer ${BLOG_WRITER_TOKEN}" \
  -H 'Content-Type: application/json' \
  -d '{"title":"A practical C++ API","author":"writer","content":"Post content"}' \
  http://127.0.0.1:8080/posts
```

## Limits

- Title: 1 to 200 UTF-8 bytes.
- Author: 1 to 100 UTF-8 bytes.
- Content: up to 1 MiB of UTF-8 text.
- Tags: up to 50 tags, each containing 1 to 64 UTF-8 bytes.
- Published date: canonical UTC `YYYY-MM-DDTHH:MM:SSZ`. If omitted, the server assigns
  the current UTC time. MongoDB stores it as a native BSON Date.
- List page size: 1 to 100 posts.
- Request body: approximately 1 MiB plus 16 KiB of JSON structure overhead.
- Request headers: up to 50 fields and 16 KiB combined.
- Server read and write timeouts: 5 seconds.
- Keep-alive: up to 20 requests or 10 seconds.

## Error Format

```json
{
  "error": {
    "code": "invalid_argument",
    "message": "invalid post id",
    "request_id": "request-1"
  }
}
```

Repository errors are mapped as follows:

| Condition | HTTP status |
| --- | ---: |
| Invalid input | `400` |
| Unsupported request content type | `415` |
| Resource not found | `404` |
| Duplicate or conflicting value | `409` |
| Database unavailable | `503` |
| Unexpected internal error | `500` |
| Missing or invalid credentials | `401` |
| Insufficient role or denied CORS origin | `403` |
| Rate limit exceeded | `429` |

## Liveness

### `GET /health`

Reports whether the HTTP process can serve requests. It does not access MongoDB.

Response: `200 OK` with the text `ok`.

## Readiness

### `GET /ready`

Runs a MongoDB ping. It returns `200 OK` when the application is ready to serve data
and `503 Service Unavailable` when the database cannot be reached.

## Metrics

### `GET /metrics`

Returns Prometheus text-format counters for HTTP and gRPC requests and errors, total
HTTP request duration, MongoDB operations and errors, and cache hits, misses,
evictions, and invalidations.

Metrics are process-local. The HTTP process exposes them on its public listener. The
gRPC process exposes the same `/metrics` format on its separate metrics listener,
which defaults to `0.0.0.0:9090` and remains internal in Docker Compose.

## Create a Post

### `POST /posts`

The request must use `Content-Type: application/json`.

```json
{
  "title": "A practical C++ API",
  "author": "writer",
  "content": "Post content",
  "tags": ["cpp", "backend"],
  "published_date": "2026-09-10T12:00:00Z"
}
```

Successful response: `201 Created`.

```json
{
  "id": "68c1c8107da74a03ce0cb86f"
}
```

## Get a Post

### `GET /posts/:id`

Returns `200 OK` with all supported post fields, `400 Bad Request` for a malformed
ObjectId, or `404 Not Found` for a valid ObjectId that does not exist.

## Replace a Post

### `PUT /posts/:id`

Replaces the mutable post fields. The JSON body uses the same format and validation
rules as `POST /posts`; the ID is taken from the URL. Author and published date are
currently mutable.

An idempotent update succeeds even when the submitted values equal the stored values.

## Delete a Post

### `DELETE /posts/:id`

Successful response: `200 OK`.

```json
{
  "deleted": true,
  "id": "68c1c8107da74a03ce0cb86f"
}
```

## List Posts

### `GET /posts?limit=20&offset=0&author=writer&tag=cpp`

`limit` defaults to 20 and cannot exceed 100. `offset` defaults to zero and must be
non-negative. Optional exact-match `author` and `tag` filters can be combined with
inclusive `published_from` and `published_to` UTC timestamp bounds. Results are sorted
by publication date and ID, both descending.

```json
{
  "items": [],
  "limit": 20,
  "offset": 0,
  "count": 0
}
```

`count` is the number of items in the current response, not the total collection
size.

## Posts per Author

### `GET /stats/posts-per-author`

Returns an array so author names remain ordinary JSON values rather than dynamic
object keys.

```json
[
  {
    "author": "writer",
    "count": 3
  }
]
```

## gRPC Error Mapping

The service contract is generated from `proto/blog_service.proto`:

| RPC | Request | Response | Purpose |
| --- | --- | --- | --- |
| `AddPost` | `Post` | `PostResponse` | Create a post and return its ID |
| `GetPost` | `PostResponse` | `FullPostResponse` | Read a post by ID |
| `UpdatePost` | `Post` | `PostResponse` | Replace a post and return its ID |
| `DeletePost` | `PostResponse` | `PostResponse` | Delete a post and return its ID |
| `GetAllPosts` | `PostResponse` | `AllPostsResponse` | Return the backward-compatible default page |
| `ListPosts` | `ListPostsRequest` | `AllPostsResponse` | Return a bounded, filtered page |

`PostResponse` is used as an ID request for historical compatibility. The request ID
is its `id` field. A C++ client attaches authentication and request tracing to its
context before invoking a protected RPC:

```cpp
grpc::ClientContext context;
context.AddMetadata("authorization", "Bearer " + writer_token);
context.AddMetadata("x-request-id", "example-request-1");

blog::Post post;
post.set_title("A practical C++ API");
post.set_author("writer");
post.set_content("Post content");

blog::PostResponse response;
grpc::Status status = stub->AddPost(&context, post, &response);
```

The sample `blog_grpc_client` exercises create, read, list, update, and delete against
an insecure local server with authentication disabled. Production clients must use
TLS channel credentials and attach the required metadata.

### Status mapping

The gRPC service uses the same repository result model:

| Condition | gRPC status |
| --- | --- |
| Invalid input | `INVALID_ARGUMENT` |
| Resource not found | `NOT_FOUND` |
| Duplicate or conflicting value | `ALREADY_EXISTS` |
| Database unavailable | `UNAVAILABLE` |
| Unexpected internal error | `INTERNAL` |
| Missing or invalid credentials | `UNAUTHENTICATED` |
| Insufficient role | `PERMISSION_DENIED` |
| Rate limit exceeded | `RESOURCE_EXHAUSTED` |

gRPC clients may send an `x-request-id` metadata value of up to 128 bytes. The server
returns the accepted or generated ID as initial metadata and includes it in the
structured request log.

The backward-compatible `GetAllPosts` RPC returns the default bounded page. The
`ListPosts` RPC accepts `limit`, `offset`, `author`, `tag`, `published_from`, and
`published_to` fields and uses the same validation, filtering, and ordering as HTTP.
