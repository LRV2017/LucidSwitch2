// Original LucidSwitch2 LXP register demonstration.
// This is standard AArch64 GNU assembler source. The matching three
// instruction words are also emitted by --create-homebrew-sample:
//
//   0xD2800540  MOVZ X0, #42
//   0xD2981BC1  MOVZ X1, #0xC0DE
//   0x14000000  B .

    .text
    .global _start
_start:
    movz x0, #42
    movz x1, #0xC0DE
1:
    b 1b

// Run the generated LXP package with an instruction budget of 3 to observe
// X0=0x2A and X1=0xC0DE, then let the host pause the session.
