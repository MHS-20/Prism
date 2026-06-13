# Prism — roadmap

## Protocol / compatibility

- RESP (Redis Serialization Protocol) compatibility layer — let `redis-cli` talk directly
- Pub/sub channels (`SUBSCRIBE`, `PUBLISH`, `UNSUBSCRIBE`)

## Persistence

- **Append-only file (AOF)** — log every write command to a file, replay on restart. Straightforward, high pedagogical value
- **Snapshot (RDB-style)** — periodic `fork()` + dump entire dataset to disk

## Data structures

- **List** (linked list or ziplist) with `LPUSH`, `LPOP`, `LRANGE`, `LLEN`
- **Hash** — the `HMap` already exists; just wire `HSET`, `HGET`, `HDEL`, `HGETALL` commands
- **Bitmap** — `SETBIT`, `GETBIT`, `BITCOUNT`

## Commands (easy additions)

- `EXISTS`, `TYPE`, `STRLEN`, `RENAME`
- `SCAN` — cursor-based key iteration (avoids blocking for large key sets)
- `DEBUG` / `OBJECT` — inspect internal data structures

## Operational

- `CONFIG GET` / `CONFIG SET` — runtime port, max-memory, idle timeout
- `INFO` — memory usage, key count, uptime, per-command stats
- `SAVE` / `BGSAVE` — trigger persistence on demand
- `MONITOR` — stream every command to a watching client
- `AUTH` — plaintext password gate
- `CLIENT LIST` / `CLIENT KILL`

## Client library (`client/`)

- Connection pooling / auto-reconnect
- Async (non-blocking) API
- Pipelining helper — batch commands without waiting between individual responses

## Internals / polish

- Per-command metrics (calls count, total latency, latest latency)
- `maxmemory` eviction policy (`allkeys-lru`, `volatile-ttl`) using the existing TTL heap
- Switch from `poll()` to `epoll` (Linux) or `kqueue` (macOS)
- Configurable listen port via CLI flag or env var
- Rate limiting per connection
- Lua scripting (advanced — full embedded Lua VM for `EVAL`/`EVALSHA`)

## Tooling

- Dockerfile + `docker-compose.yml`
- `redis-benchmark` compat — requires RESP layer first
- Fuzz tester — random valid/invalid commands against the wire protocol
- CI workflow (GitHub Actions): build → test → (optionally) fuzz

## Licence

MIT (unchanged)
