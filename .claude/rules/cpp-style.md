---
description: C++ style and safety rules
paths:
  - "**/*.cc"
  - "**/*.h"
---

- C++20 only. Do not use later-standard features.
- Formatting and naming are defined by `.clang-format` and `.clang-tidy` (Google-based). Do not hand-format against them or edit those configs without asking.
- Compiler warnings and clang-tidy findings must be fixed and are never suppressed without a good reason.
- RAII everywhere. Ownership must be explicit in the type. Ask before introducing `shared_ptr`.
- Be const-correct.
- Use `[[nodiscard]]` on functions whose result must be used.
- Comments explain why, not what.
- Include what you use; keep headers minimal.
- Treat UB, lifetime issues, and data races as bugs even if tests pass.

## Error handling

- Exceptions are only for errors that are both truly erroneous and fatal: the server cannot continue (e.g. an invalid configuration value). Such an exception propagates to `main`, is logged, and the process exits non-zero.
- Expected situations are never exceptions. `--help` and `--version` are normal control flow.
- Non-fatal errors are never exceptions either. Malformed RESP input or a bad client command is reported to the client as an error reply, and the server keeps running. Signal these with return values, not throws.
- If a function can fail in an expected way, make that visible in its return type. Ask before introducing a new error-reporting type.
