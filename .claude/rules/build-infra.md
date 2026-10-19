---
description: Build system, CI, and devcontainer conventions
paths:
  - ".devcontainer/**"
  - ".vscode/**"
  - ".github/**"
  - "CMakePresets.json"
  - "**/CMakeLists.txt"
  - "**/*.cmake"
---

- Keep the repo machine-agnostic: no host-specific paths, UIDs, or personal preferences in `.devcontainer`, `.vscode`, or CI.
- Do not pin toolchain versions (GCC, LLVM, etc.): the Debian stable base keeps major versions consistent. Library dependencies must be pinned.
