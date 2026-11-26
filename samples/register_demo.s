// Original LucidSwitch2 LXP register demonstration.
// This is readable AArch64 assembly source; the CLI can package the same
// three instructions with --create-homebrew-sample.
//
// The AArch64 instructions are:
//
//   movz x0, #42
//   movz x1, #0xC0DE
// 1:
//   b 1b
//
// The CLI-generated payload uses these little-endian instruction words:
//   0xD2800540  MOVZ X0, #42
//   0xD2981BC1  MOVZ X1, #0xC0DE
//   0x14000000  B .
//
// This sample intentionally loops. Run it with an instruction budget of 3
// to observe X0=0x2A and X1=0xC0DE, then let the host pause the session.
