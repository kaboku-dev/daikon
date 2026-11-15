# Daikon

A simple key-value store and data structure server (a mini-Redis clone) written in C++20.

Daikon is a portfolio project built to practice and demonstrate systems programming in C++: memory management, data structures, algorithms and networking. The guiding idea is **minimum features, maximum craftsmanship**: a small, readable codebase rather than a feature-complete Redis replacement.

> **Status:** early development. A single-client TCP echo server works; storage and Redis commands are not implemented yet. See the [Roadmap](#roadmap).

## Building

Requirements: a C++20 compiler (GCC/Clang) and CMake >= 3.25.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/daikon --help
```

For development, use the CMake presets. A workflow preset configures, builds and runs the tests in one step:

```sh
cmake --workflow --preset gcc-debug         # GCC, debug
cmake --workflow --preset clang-asan-ubsan  # Clang with ASan and UBSan
```

A devcontainer with the full toolchain is provided in `.devcontainer/`.

## Goals

- Be compatible with `redis-cli` and `redis-benchmark` via the RESP2 protocol
- Implement the core data structures from scratch where it is educational:
  - hash table (backing String, Hash and Set)
  - skip list (backing Sorted Set)
  - SDS (Simple Dynamic String, as in Redis) instead of `std::string`
- Keep the code well tested and clean under sanitizers (ASan, UBSan)

## Design

- Linux only
- RESP2 only
- Single-threaded event loop (reactor pattern on `epoll`)
- Key expiration: passive (on access) and active (periodic sampling of keys with a TTL, run as a timed task inside the event loop; no background thread)

## Roadmap

Development is incremental: each step is a small, reviewable change, and milestones are tagged as releases.

- [x] Project infrastructure: CMake presets, CI (GCC and Clang, ASan/UBSan), devcontainer
- [x] Command-line options, logging, graceful shutdown on signals
- [x] Single-client TCP echo server
- [ ] RESP2 parser and serializer as an isolated module, with unit tests and a fuzzing harness
- [ ] `PING`, `GET`, `SET` on a temporary `std::unordered_map` store, usable from `redis-cli` (**v0.1.0**)
- [ ] Non-blocking sockets and an `epoll` event loop serving many clients
- [ ] Custom hash table replacing the temporary store, with benchmarks
- [ ] Hash and Set data types (`HSET`, `HGET`, `SADD`, `SREM`, ...)
- [ ] SDS-based strings for keys and values
- [ ] Key expiration:
  - passive: expired keys are removed when accessed
  - active: a periodic task in the event loop samples keys with a TTL and removes expired ones
- [ ] Sorted Set on a custom skip list (`ZADD`, `ZRANGE`, `ZRANK`, `ZSCORE`)
- [ ] Small release Docker image (multi-stage Alpine build)

Possible later additions:

- List commands, then Pub/Sub
- Unix domain socket support
- Approximate LRU eviction

## License

[MIT](LICENSE)
