# LucidSwitch2

An experimental, clean-room emulator research project focused on building a lawful foundation for studying modern console software and hardware interfaces.

> **Status: early development scaffold.** LucidSwitch2 does not currently emulate Nintendo Switch 2 hardware, load commercial Switch 2 games, or provide playable compatibility. The CPU, GPU, system-service, and console-software interfaces are intentionally explicit placeholders until independently implemented and validated.

## Project principles

- **Clean-room implementation:** original code and independently authored documentation only.
- **No bundled copyrighted material:** no console firmware, encryption keys, game images, SDK files, leaked materials, or proprietary assets.
- **User-supplied files stay local:** future support must require users to provide lawfully obtained files themselves.
- **Small, testable steps:** each subsystem should have isolated tests and documented evidence before compatibility is claimed.
- **No compatibility claims without proof:** a feature is not considered supported just because an interface exists.

## What exists today

The first milestone is a portable C++20 foundation:

- checked memory-bus mapping and bounds handling;
- emulator lifecycle/state model;
- CPU and graphics backend contracts;
- a small command-line shell with a self-test mode;
- unit tests that do not require console firmware or game files.

These pieces are infrastructure, **not a functioning console emulator**.

## Build

Requirements:
- CMake 3.20 or newer
- A C++20 compiler (Visual Studio 2022 17.4+, Clang 15+, or GCC 12+)

### Windows (PowerShell)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\lucidswitch2.exe --self-test
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/lucidswitch2 --self-test
```

## Roadmap

1. **Foundation (current):** build system, tests, memory primitives, lifecycle, subsystem contracts.
2. **Architecture research:** document publicly observable hardware requirements and define testable interfaces without relying on leaked or proprietary material.
3. **CPU research:** independently implement and validate the required instruction-set behavior against public specifications and synthetic tests.
4. **Memory and system services:** add documented abstractions with deterministic tests.
5. **Graphics research:** design a backend around public graphics standards, then validate with original test scenes.
6. **Homebrew test environment:** define a documented, original test format and sample programs that contain no proprietary code or assets.
7. **Compatibility work:** only after the above, test legally obtained user-provided software and publish reproducible results.

Roadmap order may change as reliable public technical information becomes available.

## Contributing

Please open an issue before a large change. Include:
- the subsystem being changed;
- the public specification or original test supporting the change;
- build and test commands;
- known limitations.

Do not submit firmware, keys, game dumps, leaked SDKs, or proprietary source/assets.

## Legal and naming

LucidSwitch2 is an independent research project and is not affiliated with, endorsed by, or sponsored by Nintendo. “Nintendo Switch” and related marks belong to their respective owner. This repository does not distribute Nintendo software, firmware, keys, or games. Users are responsible for complying with applicable laws and the terms governing any files they provide.

## License

The source code is licensed under the MIT License. See [LICENSE](LICENSE).
