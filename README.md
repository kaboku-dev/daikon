# Daikon

A simple key-value store and data structure server (a mini-Redis clone) written in C++20.

Daikon is a portfolio project built to practice and demonstrate systems programming in C++: memory management, data structures, algorithms and networking. The guiding idea is **minimum features, maximum craftsmanship**: a small, readable codebase rather than a feature-complete Redis replacement.

> **Status:** early development. Most of the features are not implemented yet.

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
- Key expiration: passive (on access) and active (background sampling)

## License

[MIT](LICENSE)
