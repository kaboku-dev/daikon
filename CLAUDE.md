# Daikon

Daikon is a mini-Redis clone in C++20 (RESP2), built as a portfolio project to demonstrate C++ skills.

## Layout and build

- All sources and headers are in `src/` (flat). Tests are in `tests/`.
- Build and test through `CMakePresets.json` (CMake + Ninja). GCC for daily development, Clang for the CI sanitizer matrix, lld as linker.

## Principles

- Readability first. Prefer the simplest solution; add complexity only when a real requirement or a measurement justifies it.
- No features beyond the current milestone.
- No premature optimization.
- Performance matters, but measure before optimizing (Google Benchmark, redis-benchmark).
- Hash table, skip list and SDS are custom implementations. Use `std::` facilities for everything else.

## Collaboration

- Do what the current request asks, nothing more.
- Explain the reasoning behind non-trivial decisions. When there are real alternatives, present them with trade-offs.
- Ask before architectural changes: new modules, changed public interfaces, new dependencies, changed ownership or threading model.
- Everything is in English: code, comments, docs, commit messages, PRs.

## Git

- You may build and run tests.
- Never commit, push, merge, rebase, or amend. You may stage files and propose a commit message, then ask for confirmation.
- Never create or merge pull requests. Only write the PR description; the user opens the PR.
- Commit messages: short, informative, no strict format.
