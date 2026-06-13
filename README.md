# Prism

A minimal key-value Cache and Data Structure Server.

<div align="center">
<img src="prism.png" alt="Storage Logo" width="250">
</div>

## Persistence

- **Append-only file (AOF)** — every write command (`set`, `del`, `pexpire`, `zadd`, `zrem`) is logged to `prism.aof`. On restart, the file is replayed to restore state.
- **Snapshot (RDB)** — `save` dumps all keys to `prism.rdb` synchronously. `bgsave` forks a child process to do the dump in the background.

## Features

- String key-value storage (`get`, `set`, `del`)
- Sorted sets with score-based ordering (`zadd`, `zrem`, `zscore`, `zquery`)
- Per-key TTL with millisecond precision (`pexpire`, `pttl`)
- Key enumeration (`keys`)
- Custom binary protocol (no Redis serialisation, no HTTP)
- Non-blocking I/O with `poll()` — single-threaded event loop
- Thread pool (4 workers) for async destruction of large sorted sets
- Progressive rehashing hash table — no stop-the-world resizes
- Idle connection timeout (5 s)
- Zero external dependencies — standard C/C++ libs + pthreads

## Wire Protocol

Every message — request and response — follows the same frame format:

```
+----------------+----------------+----------------+-----+----------------+
| msg_len (4 LE) | nargs (4 LE)   | arg1_len (4 LE)| ... | argN data      |
+----------------+----------------+----------------+-----+----------------+
```

- **msg_len** — `uint32` little-endian, length of the body (everything after this field)
- **nargs** — `uint32` LE, number of arguments
- For each argument: a `uint32` LE length prefix followed by that many bytes of data

Responses use a type-tagged serialisation format inside the frame:

| Tag (1 byte) | Name | Payload |
|---|---|---|
| `0x00` | NIL | *(empty)* |
| `0x01` | ERR | `code(u32)` + `msg_len(u32)` + `msg` |
| `0x02` | STR | `len(u32)` + `data` |
| `0x03` | INT | `i64` (8 bytes LE) |
| `0x04` | DBL | `f64` (8 bytes LE) |
| `0x05` | ARR | `n(u32)` elements |

Maximum message size: 32 MB. Maximum argument count: 200 000.

## Commands

All arguments are strings. Numbers are parsed from strings.

| Command | Args | Description |
|---|---|---|
| `get` | `key` | Return string value or nil |
| `set` | `key` `value` | Create or overwrite a string key |
| `del` | `key` | Delete a key. Returns 1 if deleted, 0 otherwise |
| `pexpire` | `key` `ttl_ms` | Set TTL in milliseconds on a key. Returns 1 if exists |
| `pttl` | `key` | Return remaining TTL in ms, `-1` if no TTL, `-2` if not found |
| `keys` | *(none)* | Return array of all keys |
| `zadd` | `zset` `score` `name` | Add member to sorted set. Returns 1 if new, 0 if updated |
| `zrem` | `zset` `name` | Remove member. Returns 1 if existed |
| `zscore` | `zset` `name` | Return score of member, or nil |
| `zquery` | `zset` `score` `name` `offset` `limit` | Query members >= `(score, name)` with pagination. Returns flat array of `[name, score, name, score, ...]` |
| `lpush` | `key` `val [val ...]` | Push values to head of list. Returns new length |
| `lpop` | `key` | Pop value from head of list. Returns nil if empty |
| `llen` | `key` | Return list length |
| `lrange` | `key` `start` `stop` | Return range of list elements (inclusive indices) |
| `hset` | `key` `field` `val` | Set hash field to value. Returns 1 if new field, 0 if updated |
| `hget` | `key` `field` | Get hash field value. Returns nil if missing |
| `hdel` | `key` `field` | Delete hash field. Returns 1 if existed |
| `hgetall` | `key` | Return flat array of `[field, val, field, val, ...]` |
| `setbit` | `key` `offset` `value` | Set bit at offset to 0 or 1. Returns old bit |
| `getbit` | `key` `offset` | Get bit at offset |
| `bitcount` | `key` `[start end]` | Count set bits in byte range |
| `exists` | `key` | Return 1 if key exists, 0 otherwise |
| `type` | `key` | Return type name: `string`, `zset`, `list`, `hash`, or `none` |
| `strlen` | `key` | Return length of string value |
| `rename` | `key` `newkey` | Rename a key. Overwrites newkey if exists |
| `scan` | `cursor` `[COUNT n]` | Incremental key iteration. Returns `[next_cursor, keys...]` |
| `debug` | *(none)* | Return server info (key count, connections) |
| `object` | `key` | Return metadata about a key (type, TTL) |
| `subscribe` | `channel [channel ...]` | Subscribe to channels. Puts connection into pub/sub mode |
| `unsubscribe` | `[channel ...]` | Unsubscribe from channels. Each channel returns `["unsubscribe", channel, count]`. If no channels given, unsubscribes from all |
| `publish` | `channel` `message` | Send a message to all subscribers of a channel. Returns the number of subscribers that received it |
| `save` | *(none)* | Synchronously dump snapshot to `prism.rdb` |
| `bgsave` | *(none)* | Fork a child to dump snapshot to `prism.rdb` in background. Returns child PID |

## Architecture

### Event loop

`poll()` dispatches between:

1. **Listening socket** — accept new connections
2. **Client sockets** — read requests, write responses, handle errors
3. **Timers** — expire idle connections (linked list) and TTL'd keys (min-heap)

### Hash table

Open-addressed chained hash table using separate chaining. Wrapped in `HMap` which maintains two `HTab` instances for **progressive rehashing**: when the active table exceeds the load factor (8), a new table is allocated and entries migrate incrementally (128 per insert/lookup/delete). No GC pause.

### Sorted set

Two-index structure:

- **AVL tree** ordered by `(score, name)` — enables range queries (`zquery`) and offset-based pagination via subtree-size caching
- **Hash map** keyed by `name` — O(1) lookup by member name (`zscore`, `zrem`, `zadd` update)

### AVL tree

Self-balancing binary search tree. Each node caches `height` (balance factor) and `cnt` (subtree size). The `cnt` field enables O(log N) rank-based traversal (`avl_offset`), used by `zquery` pagination.

### TTL heap

Binary min-heap keyed by absolute expiry timestamp. Each `Entry` stores its heap slot index for O(log N) update/removal.

### Thread pool

4 worker threads wait on a condition variable. When a large zset (≥1000 members) is deleted, the destructor is offloaded to the pool to avoid blocking the event loop.

## Build

### CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Make wrapper

```sh
make          # configure + build (Release)
make run      # build and run
make clean    # remove build directory
make rebuild  # clean then build
```

### Direct compilation (server only, no client library)

```sh
g++ -O2 -std=c++11 -pthread server/*.cpp -o prism-server
```

## Run

```sh
./build/prism-server
```

Listens on `0.0.0.0:1234`. No config file, no CLI flags.

## Client library

The `client/` directory provides a C/C++ library (`prism.h` / `prism.cpp`) for speaking the protocol. Build as a static library:

```sh
g++ -std=c++11 -c client/prism.cpp -o prism-client.o
ar rcs libprism-client.a prism-client.o
```

Or link via CMake: `target_link_libraries(your_app prism-client)`.

### API

```c
// Connection lifecycle
PrismConn *prism_connect(const char *host, uint16_t port);
void prism_close(PrismConn *conn);

// Send arbitrary command (NULL-terminated args)
PrismReply *prism_cmd(PrismConn *conn, int nargs, ...);
PrismReply *prism_cmdv(PrismConn *conn, const char **args, int nargs);

// Reply inspection
PrismType  prism_type(PrismReply *reply);
const char *prism_err_msg(PrismReply *reply, uint32_t *code);
const char *prism_str(PrismReply *reply, size_t *len);
int64_t     prism_int(PrismReply *reply);
double      prism_dbl(PrismReply *reply);
size_t      prism_arr_len(PrismReply *reply);
PrismReply *prism_arr_at(PrismReply *reply, size_t idx);
void        prism_reply_free(PrismReply *reply);

// Convenience wrappers for each command
PrismReply *prism_get(PrismConn *, const char *key);
PrismReply *prism_set(PrismConn *, const char *key, const char *val);
PrismReply *prism_del(PrismConn *, const char *key);
PrismReply *prism_pexpire(PrismConn *, const char *key, int64_t ttl_ms);
PrismReply *prism_pttl(PrismConn *, const char *key);
PrismReply *prism_keys(PrismConn *);
PrismReply *prism_zadd(PrismConn *, const char *zset, double score, const char *name);
PrismReply *prism_zrem(PrismConn *, const char *zset, const char *name);
PrismReply *prism_zscore(PrismConn *, const char *zset, const char *name);
PrismReply *prism_zquery(PrismConn *, const char *zset, double score, const char *name, int64_t offset, int64_t limit);
```

## Usage example

CMake builds both `prism-server` and `libprism-client.a`. Link your own program against the client library:

```sh
g++ -std=c++11 -Iclient example.cpp -Lbuild -lprism-client -o example
```

Or add to your `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/prism)
target_link_libraries(your_app prism-client)
```

```cpp
#include "prism.h"
#include <stdio.h>

int main() {
    PrismConn *c = prism_connect("127.0.0.1", 1234);
    if (!c) { perror("connect"); return 1; }

    PrismReply *r;

    r = prism_set(c, "hello", "world");
    prism_reply_free(r);

    r = prism_get(c, "hello");
    size_t len;
    const char *s = prism_str(r, &len);
    printf("got: %.*s\n", (int)len, s);
    prism_reply_free(r);

    r = prism_zadd(c, "scores", 99.5, "alice");
    printf("zadd new: %lld\n", prism_int(r));
    prism_reply_free(r);

    r = prism_zquery(c, "scores", 0, "", 0, 10);
    size_t n = prism_arr_len(r);
    for (size_t i = 0; i < n; i += 2) {
        const char *name = prism_str(prism_arr_at(r, i), NULL);
        double score = prism_dbl(prism_arr_at(r, i + 1));
        printf("  %s: %.2f\n", name, score);
    }
    prism_reply_free(r);

    prism_close(c);
}
```

### Pub/sub example

```c
PrismConn *sub = prism_connect("127.0.0.1", 1234);

// subscribe to a channel — connection enters pub/sub mode
PrismReply *r = prism_subscribe(sub, "news");
// r is ARR(3): ["subscribe", "news", 1]
prism_reply_free(r);

// publish from another connection
PrismConn *pub = prism_connect("127.0.0.1", 1234);
r = prism_publish(pub, "news", "hello");
printf("sent to %lld subscribers\n", prism_int(r));
prism_reply_free(r);
prism_close(pub);

// subscriber reads the push message
r = prism_read_next(sub);
// r is ARR(3): ["message", "news", "hello"]
printf("received: %s\n", prism_str(prism_arr_at(r, 2), NULL));
prism_reply_free(r);

// unsubscribing exits pub/sub mode
r = prism_unsubscribe(sub, "news");
prism_reply_free(r);

// after all channels unsubscribed, regular commands work again
r = prism_set(sub, "key", "val");
prism_reply_free(r);

prism_close(sub);
```

## Project layout

| Path | Role |
|---|---|
| `server/` | Server source (event loop, data structures, protocol) |
| `client/` | Client library (`prism.h`, `prism.cpp`) |
| `CMakeLists.txt` | Top-level CMake (builds both targets) |
| `Makefile` | Convenience wrapper for CMake |

### Data structures

| Structure | File | Used for |
|---|---|---|
| `HMap` (2× `HTab`) | `server/hashtable.h/.cpp` | Top-level KV store, zset name index |
| `AVLNode` | `server/avl.h/.cpp` | Zset ordering by `(score, name)`, rank queries |
| `ZSet` / `ZNode` | `server/zset.h/.cpp` | Sorted set abstraction |
| `HeapItem` | `server/heap.h/.cpp` | TTL expiry queue |
| `DList` | `server/list.h` | Idle connection LRU |
| `TheadPool` | `server/thread_pool.h/.cpp` | Async large-object cleanup |
