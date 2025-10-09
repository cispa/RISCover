// Implementation of minimal libopcodes-based disassembler (host arch only)
#define _GNU_SOURCE
// Binutils headers require PACKAGE/PACKAGE_VERSION if config.h is absent.
#ifndef PACKAGE
#define PACKAGE "external"
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <dis-asm.h>

#include "disasm_opcodes.h"

// Support only AArch64 or RISC-V builds; error out otherwise in one place.
#if !defined(__aarch64__) && !defined(__riscv)
#error "Unsupported architecture: build for aarch64 or riscv64 only"
#endif

// Select per-architecture disassembler function
typedef int (*print_insn_ft)(bfd_vma, disassemble_info*);

static int configure_target(disassemble_info *info, print_insn_ft *out_printer) {
#if defined(__aarch64__)
  info->arch = bfd_arch_aarch64;
  info->mach = bfd_mach_aarch64;
  info->endian = BFD_ENDIAN_LITTLE;
  info->endian_code = BFD_ENDIAN_LITTLE;
  *out_printer = disassembler (info->arch, /*big=*/false, info->mach, NULL);
#elif defined(__riscv)
  info->arch = bfd_arch_riscv;
  info->mach = bfd_mach_riscv64;
  info->endian = BFD_ENDIAN_LITTLE;
  info->endian_code = BFD_ENDIAN_LITTLE;
  *out_printer = disassembler (info->arch, /*big=*/false, info->mach, NULL);
#endif
  return 0;
}

// We capture disassembler output into a dynamic buffer via open_memstream.
// disassemble_info will call fprintf on our FILE* stream.

// styled fprintf that ignores styling and forwards to vfprintf
static int styled_fprintf_passthru(void *stream, enum disassembler_style style,
                                   const char *fmt, ...) {
  (void)style;
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf((FILE *)stream, fmt, ap);
  va_end(ap);
  return r;
}

// Configure disassemble_info for a raw buffer source.
static void setup_disasm_info_for_buffer(disassemble_info *info,
                                         const uint8_t *buf,
                                         size_t buflen,
                                         uint64_t vma,
                                         FILE *stream,
                                         const char *options) {
  memset(info, 0, sizeof(*info));
  init_disassemble_info(info, stream,
                        (fprintf_ftype) fprintf,
                        (fprintf_styled_ftype) styled_fprintf_passthru);

  // Use the built-in buffer reader so libopcodes reads from our memory.
  info->read_memory_func = buffer_read_memory;
  info->memory_error_func = perror_memory;
  info->print_address_func = generic_print_address;
  info->symbol_at_address_func = generic_symbol_at_address;
  info->symbol_is_valid = generic_symbol_is_valid;
  info->buffer = (bfd_byte *)buf;
  info->buffer_vma = vma;
  info->buffer_length = buflen;

  // Suppress address/bytes in output; we only want mnemonic+operands.
  info->endian = BFD_ENDIAN_LITTLE;
  info->endian_code = BFD_ENDIAN_LITTLE;
  info->flavour = bfd_target_unknown_flavour;
  info->section = NULL;
  info->insn_info_valid = 0; // we don't require extra info
  info->disassembler_options = (char *)options; // libopcodes expects char*
}

static int disasm4(const uint8_t *bytes, size_t len, uint64_t addr,
            char *out, size_t outsz) {
  if (!bytes || !out || outsz == 0) {
    return -1;
  }
  if (len == 0) {
    out[0] = '\0';
    return 0;
  }
  if (len > 4) len = 4; // only consider first 4 bytes by design

  // Initialize BFD library once per process.
  static int bfd_inited = 0;
  if (!bfd_inited) {
    bfd_init();
    bfd_inited = 1;
  }

  // Prepare capture stream
  char *dyn = NULL;
  size_t dynsz = 0;
  FILE *mem = open_memstream(&dyn, &dynsz);
  if (!mem) {
    return -1;
  }

  disassemble_info info;
  setup_disasm_info_for_buffer(&info, bytes, len, addr, mem, NULL);

  // Configure target selection and get the print_insn function
  print_insn_ft printer = NULL;
  if (configure_target(&info, &printer) != 0) {
    fclose(mem);
    free(dyn);
    return -1;
  }
  // Initialize target hooks after setting arch/mach/endian
  disassemble_init_for_target(&info);

  // Perform the disassembly of one instruction
  // printer returns the instruction length in bytes (or 0/-1 on error)
  int used = printer((bfd_vma)addr, &info);

  // Finalize the capture buffer
  fflush(mem);
  fclose(mem); // ensures dyn/dynsz is set

  if (used <= 0 || dyn == NULL) {
    free(dyn);
    return -1;
  }

  // dyn contains trailing newline in most disassemblers; trim whitespace
  // Copy into caller buffer
  // Trim leading/trailing spaces and tabs and newlines
  size_t start = 0;
  while (start < dynsz && (dyn[start] == ' ' || dyn[start] == '\t' || dyn[start] == '\n')) start++;
  size_t end = dynsz;
  while (end > start && (dyn[end-1] == ' ' || dyn[end-1] == '\t' || dyn[end-1] == '\n')) end--;
  size_t tocopy = end > start ? (end - start) : 0;
  if (tocopy >= outsz) tocopy = outsz - 1;
  if (tocopy > 0) {
    memcpy(out, dyn + start, tocopy);
  }
  out[tocopy] = '\0';

  free(dyn);
  return (int)tocopy;
}

char *disasm_opcodes(uint32_t instr, uintptr_t vma) {
  uint8_t buf[4];
  // Always little-endian bytes
  buf[0] = (uint8_t)(instr & 0xFF);
  buf[1] = (uint8_t)((instr >> 8) & 0xFF);
  buf[2] = (uint8_t)((instr >> 16) & 0xFF);
  buf[3] = (uint8_t)((instr >> 24) & 0xFF);
  char tmp[128];
  int n = disasm4(buf, 4, (uint64_t)vma, tmp, sizeof(tmp));
  if (n < 0) return NULL;
  size_t len = (size_t)n;
  char *res = (char *)malloc(len + 1);
  if (!res) return NULL;
  memcpy(res, tmp, len);
  res[len] = '\0';
  return res;
}

char *disasm_opcodes_opts(uint32_t instr, uintptr_t vma, const char *options) {
  uint8_t buf[4];
  buf[0] = (uint8_t)(instr & 0xFF);
  buf[1] = (uint8_t)((instr >> 8) & 0xFF);
  buf[2] = (uint8_t)((instr >> 16) & 0xFF);
  buf[3] = (uint8_t)((instr >> 24) & 0xFF);

  char *dyn = NULL;
  size_t dynsz = 0;
  FILE *mem = open_memstream(&dyn, &dynsz);
  if (!mem) return NULL;

  disassemble_info info;
  setup_disasm_info_for_buffer(&info, buf, 4, (uint64_t)vma, mem, options);

  print_insn_ft printer = NULL;
  if (configure_target(&info, &printer) != 0) {
    fclose(mem);
    free(dyn);
    return NULL;
  }
  disassemble_init_for_target(&info);

  int used = printer((bfd_vma)vma, &info);
  fflush(mem);
  fclose(mem);

  if (used <= 0 || !dyn) {
    free(dyn);
    return NULL;
  }
  // Trim and duplicate
  size_t start = 0, end = dynsz;
  while (start < dynsz && (dyn[start] == ' ' || dyn[start] == '\t' || dyn[start] == '\n')) start++;
  while (end > start && (dyn[end-1] == ' ' || dyn[end-1] == '\t' || dyn[end-1] == '\n')) end--;
  size_t n = end > start ? (end - start) : 0;
  char *res = (char *)malloc(n + 1);
  if (!res) { free(dyn); return NULL; }
  if (n) memcpy(res, dyn + start, n);
  res[n] = '\0';
  free(dyn);
  return res;
}
