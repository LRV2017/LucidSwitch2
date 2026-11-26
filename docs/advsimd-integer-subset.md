# Advanced SIMD integer subset

## Supported instructions

The A64 interpreter implements these initial Advanced SIMD vector-integer operations:

- \`ADD Vd.<T>, Vn.<T>, Vm.<T>\`
- \`SUB Vd.<T>, Vn.<T>, Vm.<T>\`
- \`AND Vd.8B/16B, Vn.8B/16B, Vm.8B/16B\`
- \`ORR Vd.8B/16B, Vn.8B/16B, Vm.8B/16B\`
- \`EOR Vd.8B/16B, Vn.8B/16B, Vm.8B/16B\`

ADD and SUB support 8-, 16-, 32-, and 64-bit integer lanes in architecturally valid 64-bit or 128-bit arrangements. The 64-bit arrangement does not permit a 64-bit lane. Logical operations treat the selected register as a raw 64-bit or 128-bit bit vector.

All implemented operations are lane-wise or bit-wise. Integer overflow wraps modulo the lane width. These instructions leave NZCV unchanged. A write to a 64-bit SIMD arrangement clears the upper 64 bits of the modeled destination register.

## Explicit limitations

This is not general NEON support. The current interpreter does not support Advanced SIMD loads/stores, lane insertion/extraction, vector floating-point, comparisons, saturating or halving arithmetic, shifts, permutations, pairwise/reduction instructions, table lookup, crypto, or FPCR/FPSR behavior. It also does not model CPACR/CPTR access traps or the complete architecture-defined FP/Advanced SIMD enable path.

The eight-context Switch 2 cluster uses this same partial interpreter. It does not gain complete Cortex-A78C execution from this subset. Real application compatibility remains blocked by the many unsupported instructions, process/runtime requirements, Horizon services, and guest Ampere GPU implementation.

## Tests

\`tests/test_a64.cpp\` uses synthetic A64 instruction words and isolated memory. The fixtures check 8-, 16-, 32-, and 64-bit integer lane arithmetic, 64/128-bit arrangements, logical vector operations, upper-half clearing for 64-bit writes, NZCV preservation, and rejection of the reserved Q=0 / 64-bit-lane arrangement.

The instructions are decoded and tested independently of commercial games, console firmware, or protected content.
