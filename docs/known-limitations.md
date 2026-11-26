# Known Limitations

This document is intentionally blunt so the project's maturity is not overstated.

- The A64 interpreter implements only the documented subset in a64-subset.md, not the complete ARMv8-A instruction set.
- The stage-1 MMU supports only one constrained 4 KiB / 48-bit TTBR0 regime with EL1 permission interpretation; it is not a complete ARMv8-A translation implementation.
- No TTBR1, ASIDs, additional translation granules, stage-2 translation, complete TLB maintenance instructions, memory attribute handling, or full architectural fault syndromes are implemented.
- MRS/MSR writes to TTBR0_EL1 and TCR_EL1 update the modeled register bank but do not yet reconfigure the active Stage1Mmu.
- Exception routing models selected EL1 state and vector slots only; there is no full exception entry/return, banked state, or privilege-transition model.
- Timer output, interrupt-controller signals, and MMIO device accesses are not yet connected to CPU execution.
- No console GPU command processor, shader translator, or renderer exists.
- No guest operating-system, kernel, or system-service implementation exists.
- No console-specific executable/package loader exists.
- No firmware, keys, game images, SDK files, leaked materials, or commercial assets are included or supported by this scaffold.
- The memory bus is a generic host-side physical backing store, not a map of any real console.
- The emulator execution loop runs synthetic programs loaded through its API; it is not a commercial game loader.
- A successful build, self-test, or CPU demo is not proof that console software can boot or run.
- No Nintendo Switch 2 hardware compatibility or commercial game compatibility is claimed.


## Graphics foundation

The guest GPU memory class is bounded host-backed storage, not a hardware memory controller. The command processor supports only an original test packet format for begin-frame, clear, filled rectangle, present, and end. The software backend is the only renderer that rasterizes. The optional Vulkan backend creates a Vulkan device and performs off-screen RGBA8 image uploads; it does not yet implement shader translation, GPU rasterization, a surface/swapchain, or console-specific GPU behavior. Vulkan is enabled only when the SDK is detected at CMake configure time.


## Graphics pipeline (Phase 5)

The reference renderer now supports indexed triangle lists, a color and floating-point depth framebuffer, texture resources, nearest/bilinear filtering, and clamp/repeat sampler addressing. The guest command parser supports create/destroy texture and indexed draws. SDL2 window presentation displays a CPU readback of the completed frame. The Vulkan backend still uses CPU rasterization and synchronous staging uploads; no native Vulkan shader pipeline or swapchain is present yet.
