# Contributing

Thanks for helping improve LucidSwitch2.

## Before opening a pull request

- Explain the subsystem and behavior being changed.
- Link the public specification or describe the original test that supports the implementation.
- Add deterministic tests for new behavior.
- Run the full CMake build and CTest suite.
- Document unsupported cases and assumptions.

## Project boundaries

Do not contribute firmware, encryption keys, game dumps, leaked SDKs, proprietary source code, or copyrighted assets. Do not add functionality intended to bypass access controls or obtain console content without authorization.

## Code style

- C++20
- Prefer small interfaces and explicit error results.
- Avoid silent fallbacks for unsupported behavior.
- Keep generic host utilities independent of undocumented console-specific assumptions.
