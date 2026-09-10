# Persistence and Schema Management

MongoDB schema management runs at application startup through
`BlogRepository::InitializeSchema()`. Startup fails before either server accepts
traffic when migration, validation, or index creation fails.

## Schema Version 1

Migration `1`, named `native_post_dates_and_indexes`, is recorded in the
`_schema_migrations` collection. It is idempotent and performs these changes:

- Converts legacy string `published_date` values to BSON Date values.
- Converts embedded comment timestamps to BSON Date values.
- Replaces missing or unparseable legacy timestamps with the migration time.
- Installs collection validators and the required indexes.

The public HTTP and gRPC contracts continue to use canonical UTC strings in the
`YYYY-MM-DDTHH:MM:SSZ` format. Conversion occurs only at the persistence boundary.

## Collection Validation

Both collections use strict MongoDB JSON Schema validation for new writes and
updates:

- `users` requires a username, email, password hash, and profiles array.
- `posts` requires title, author, content, tags, a BSON publication date, and an
  embedded comments array.
- A post is limited to 50 tags and 100 embedded comments.
- Comment timestamps must be BSON dates.

Comments remain embedded because the current model loads them with a post and caps
their growth at 100 entries. If independent comment pagination or unbounded comment
growth becomes a requirement, move comments to a separate collection in a new,
versioned migration.

## Indexes

| Collection | Index name | Keys | Purpose |
| --- | --- | --- | --- |
| `users` | `users_username_unique` | `username: 1` | Enforce unique usernames and support login lookup |
| `posts` | `posts_published_id` | `published_date: -1, _id: -1` | Deterministic unfiltered pagination |
| `posts` | `posts_author_published` | `author: 1, published_date: -1` | Author-filtered pages |
| `posts` | `posts_tags_published` | `tags: 1, published_date: -1` | Tag-filtered pages |

Startup creates these indexes idempotently and verifies their names afterward.

## Query Behavior

Post lists are always bounded to 1–100 results and sorted by `published_date`
descending, then `_id` descending. Supported filters are exact author, exact array tag,
and inclusive `published_from`/`published_to` UTC bounds. Offset pagination is retained
for API compatibility; cursor pagination can replace it if deep-page performance
becomes material.

