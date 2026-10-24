---
description: Testing conventions
paths:
  - "tests/**"
---

- GoogleTest. Test observable behavior, not implementation details.
- Name test files after the unit under test with a `_test.cc` suffix.
- Cover edge cases and error paths, not just the happy path. For the RESP parser, include malformed input: it must produce an error result, never a throw or a crash.
- No sleeps or timing-dependent assertions.
- Tests must pass under ASan and UBSan.
- Benchmarks (Google Benchmark) are separate from unit tests.
