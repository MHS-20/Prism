# Prism

A minimal key-value Cache and Data Structure Server.

<div align="center">
<img src="prism.png" alt="Storage Logo" width="250">
</div>

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

## Build

### CMake (recommended)

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

### Direct compilation (no build system)

```sh
g++ -O2 -std=c++11 -pthread src/*.cpp -o prism
```

## Run

```sh
./build/prism
```

Listens on `0.0.0.0:1234`. No config file, no CLI flags.

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

## Architecture

```
src/
├── common.h        — container_of macro, FNV-1a hash
├── list.h          — intrusive doubly-linked list (idle connections)
├── avl.h / .cpp    — AVL tree (sorted-set order, rank queries)
├── hashtable.h/.cpp— 2-table hash map with progressive rehashing
├── heap.h / .cpp   — binary min-heap (TTL expiry)
├── zset.h / .cpp   — sorted set: AVL tree by (score, name) + hash map by name
├── thread_pool.h/.cpp — fixed-size thread pool (zset cleanup)
└── server.cpp      — entrypoint, poll event loop, protocol, commands
```

### Event loop (`server.cpp:802`)

`poll()` dispatches between:

1. **Listening socket** — accept new connections
2. **Client sockets** — read requests, write responses, handle errors
3. **Timers** — expire idle connections (linked list) and TTL'd keys (min-heap)

### Hash table (`hashtable.cpp`)

Open-addressed chained hash table using separate chaining. Wrapped in `HMap` which maintains two `HTab` instances for **progressive rehashing**: when the active table exceeds the load factor (8), a new table is allocated and entries migrate incrementally (128 per insert/lookup/delete). No GC pause.

### Sorted set (`zset.cpp`)

Two-index structure:

- **AVL tree** ordered by `(score, name)` — enables range queries (`zquery`) and offset-based pagination via subtree-size caching
- **Hash map** keyed by `name` — O(1) lookup by member name (`zscore`, `zrem`, `zadd` update)

### AVL tree (`avl.cpp`)

Self-balancing binary search tree. Each node caches `height` (balance factor) and `cnt` (subtree size). The `cnt` field enables O(log N) rank-based traversal (`avl_offset`), used by `zquery` pagination.

### TTL heap (`heap.cpp`)

Binary min-heap keyed by absolute expiry timestamp. Each `Entry` stores its heap slot index for O(log N) update/removal.

### Thread pool (`thread_pool.cpp`)

4 worker threads wait on a condition variable. When a large zset (≥1000 members) is deleted, the destructor is offloaded to the pool to avoid blocking the event loop.

## Example client

Using a shell with `od` and `/dev/tcp`:

```sh
# set foo bar
printf '\x07\x00\x00\x00'                           # msg_len = 7
printf '\x02\x00\x00\x00'                           # nargs = 2
printf '\x03\x00\x00\x00foo'                        # arg: "foo"
printf '\x03\x00\x00\x00bar'                        # arg: "bar"
# (piped to /dev/tcp/localhost/1234, read response)
```

Or with Python:

```python
import socket, struct

s = socket.socket()
s.connect(('127.0.0.1', 1234))

def send_cmd(*args):
    body = struct.pack('<I', len(args))
    for a in args:
        body += struct.pack('<I', len(a)) + a.encode()
    s.sendall(struct.pack('<I', len(body)) + body)

def read_response():
    data = s.recv(4)
    (n,) = struct.unpack('<I', data)
    return s.recv(n)

send_cmd('set', 'hello', 'world')
print(read_response())   # b'\x00' (nil)

send_cmd('get', 'hello')
print(read_response())   # b'\x02\x05\x00\x00\x00world' (STR)
```

## Data structures summary

| Structure | File | Used for |
|---|---|---|
| `HMap` (2× `HTab`) | `hashtable.h/.cpp` | Top-level KV store, zset name index |
| `AVLNode` | `avl.h/.cpp` | Zset ordering by `(score, name)`, rank queries |
| `ZSet` / `ZNode` | `zset.h/.cpp` | Sorted set abstraction |
| `HeapItem` | `heap.h/.cpp` | TTL expiry queue |
| `DList` | `list.h` | Idle connection LRU |
| `TheadPool` | `thread_pool.h/.cpp` | Async large-object cleanup |

## Licence

MIT
