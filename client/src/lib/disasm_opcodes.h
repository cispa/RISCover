// Minimal libopcodes-based disassembler for 4-byte instructions (host arch)
// Public API

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Disassembles a single 32-bit instruction value at virtual address `vma`.
// Returns a heap-allocated C string (mnemonic + operands) on success, or NULL on error.
// Caller must free() the returned string.
char *disasm_opcodes(uint32_t instr, uintptr_t vma);

// Same as disasm_opcodes but allows passing libopcodes disassembler options
// (comma-separated, like objdump -M). Example: "no-aliases,reg-names=abi".
// The options pointer may be NULL for defaults.
char *disasm_opcodes_opts(uint32_t instr, uintptr_t vma, const char *options);

#ifdef __cplusplus
}
#endif
