# Known Limitations

This document is intentionally blunt so the project's maturity is not overstated.

- The A64 interpreter implements only the documented subset in a64-subset.md, not the complete ARMv8-A instruction set.
- No exception levels, privilege model, system registers, MMU/TLB, cache-coherency model, atomics, SIMD/FP, or interrupt controller exists.
- No console GPU command processor, shader translator, or renderer exists.
- No guest operating-system, kernel, or system-service implementation exists.
- No console-specific executable/package loader exists.
- No firmware, keys, game images, SDK files, leaked materials, or commercial assets are included or supported by this scaffold.
- The memory bus is a generic host-side address-space utility, not a map of any real console.
- The emulator execution loop runs synthetic programs loaded through its API; it is not a commercial game loader.
- A successful build, self-test, or CPU demo is not proof that console software can boot or run.
- No Nintendo Switch 2 hardware compatibility or commercial game compatibility is claimed.
