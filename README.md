# LucidSwitch2

An experimental, clean-room emulator research project focused on building a lawful foundation for studying modern console software and hardware interfaces.

> **Status: early development.** LucidSwitch2 includes a small A64 interpreter for synthetic test programs, but it does not currently emulate Nintendo Switch 2 hardware, load commercial Switch 2 games, or provide playable compatibility. CPU, GPU, memory-map, system-service, and console-software compatibility remain incomplete.

## Project principles

- **Clean-room implementation:** original code and independently authored documentation only.
- **No bundled copyrighted material:** no console firmware, encryption keys, game images, SDK files, leaked materials, or proprietary assets.
- **User-supplied files stay local:** future support must require users to provide lawfully obtained files themselves.
- **Small, testable steps:** each subsystem should have isolated tests and documented evidence before compatibility is claimed.
- **No compatibility claims without proof:** a feature is not considered supported just because an interface exists.

## Current implementation

- C++20 / CMake project foundation.
- Checked host-side memory bus.
- Emulator lifecycle model.
- **A64 interpreter subset:** NOP, MOVZ, MOVK, ADD/SUB immediate, B, CBZ/CBNZ, BR, RET.
- Synthetic instruction tests.
- Cross-platform GitHub Actions CI.

The interpreter is an educational milestone, **not a complete ARMv8-A CPU**. See [A64 subset limitations](docs/a64-subset.md).

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

1. **Foundation:** build system, tests, memory primitives, lifecycle, subsystem contracts.
2. **CPU research:** expand the A64 interpreter with flags, register arithmetic, memory operations, and public-specification-based tests.
3. **Architecture research:** document publicly observable hardware requirements and define testable interfaces.
4. **Memory and system services:** add documented abstractions with deterministic tests.
5. **Graphics research:** design a backend around public graphics standards, then validate with original test scenes.
6. **Homebrew test environment:** define a documented, original test format and sample programs that contain no proprietary code or assets.
7. **Compatibility work:** only after the above, test legally obtained user-provided software and publish reproducible results.

## Contributing

Please open an issue before a large change. Include the subsystem, public specification or original test, build/test commands, and known limitations. Do not submit firmware, keys, game dumps, leaked SDKs, or proprietary source/assets.

## Legal and naming

LucidSwitch2 is an independent research project and is not affiliated with, endorsed by, or sponsored by Nintendo. “Nintendo Switch” and related marks belong to their respective owner. This repository does not distribute Nintendo software, firmware, keys, or games. Users are responsible for complying with applicable laws and the terms governing any files they provide.

## License

The source code is licensed under the MIT License. See [LICENSE](LICENSE).
