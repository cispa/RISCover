#ifndef UTIL_H_
#define UTIL_H_

#include <assert.h>
#undef assert
#define assert(condition) \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d\tAssertion %s failed\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    }

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "dbg.h"
#include "asm_util.h"

#if !defined(__aarch64__) && !defined(__riscv)
#error "Unsupported architecture."
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define UNIQUE_LABEL_MERGE(x, y) x##_##y
#define UNIQUE_LABEL_EXPAND(x, y) UNIQUE_LABEL_MERGE(x, y)
#define UNIQUE_LABEL(prefix) UNIQUE_LABEL_EXPAND(prefix, __COUNTER__)

#define asizeof(array) (sizeof(array)/sizeof(array[0]))

#define stack_size (1<<20)
#if defined(__riscv)
#define page_size 4096
#elif defined(__aarch64__)
// Apple chips have 16K pages
#define page_size 16384
#endif

void print_hexbuf(uint32_t* ptr, int n);
void print_hexbuf_group(uint8_t* ptr, int n, int size);

void print_hex(uint8_t* ptr, int n);
void fprint_hex_plain(FILE* file, uint8_t* ptr, int n);
void fprint_hex(FILE* file, uint8_t* ptr, int n);

void print_vec(uint8_t* ptr, int n);
void fprint_vec(FILE* file, uint8_t* ptr, int n);

char* my_strsignal(int sig);

#define SPLIT_HEX(hex) \
    ((unsigned char)(hex & 0xFF)), ((unsigned char)((hex >> 8) & 0xFF)),  \
    ((unsigned char)((hex >> 16) & 0xFF)), ((unsigned char)((hex >> 24) & 0xFF)),  \
    ((unsigned char)((hex >> 32) & 0xFF)), ((unsigned char)((hex >> 40) & 0xFF)),  \
    ((unsigned char)((hex >> 48) & 0xFF)), ((unsigned char)((hex >> 56) & 0xFF))
#define INIT_16(hex1, hex2) \
    {{ SPLIT_HEX((uint64_t)hex2), SPLIT_HEX((uint64_t)hex1) }}
#define ASSIGN_8(reg, hex) \
    static_assert(sizeof(reg) >= 8, "reg not 8 byte"); \
    ((uint64_t*)&reg)[0] = hex;
#define ASSIGN_16(reg, hex1, hex0) \
    static_assert(sizeof(reg) >= 16, "reg not 16 byte"); \
    ((uint64_t*)&reg)[1] = hex1; \
    ((uint64_t*)&reg)[0] = hex0;
#define ASSIGN_24(reg, hex2, hex1, hex0) \
    static_assert(sizeof(reg) >= 24, "reg not 24 byte"); \
    ((uint64_t*)&reg)[2] = hex2; \
    ((uint64_t*)&reg)[1] = hex1; \
    ((uint64_t*)&reg)[0] = hex0;
#define ASSIGN_32(reg, hex3, hex2, hex1, hex0) \
    static_assert(sizeof(reg) >= 32, "reg not 32 byte"); \
    ((uint64_t*)&reg)[3] = hex3; \
    ((uint64_t*)&reg)[2] = hex2; \
    ((uint64_t*)&reg)[1] = hex1; \
    ((uint64_t*)&reg)[0] = hex0;

#define INCLUDE_REG_MACROS() \
    asm volatile(".include \"out/runner_arch_preprocessed.S\"\n\t");

#define SAVE_STATE(regs) \
    asm volatile("SAVE_STATE "STR(regs));

#define SAVE_STATE_EXTENDED(regs) \
    asm volatile("SAVE_STATE_EXTENDED "STR(regs));

#define RESTORE_STATE(regs) \
    asm volatile("RESTORE_STATE "STR(regs));

#define RESTORE_STATE_EXTENDED(regs) \
    asm volatile("RESTORE_STATE_EXTENDED "STR(regs));

#if defined(__riscv)
#define SAVE_STATE_VECTOR(regs) \
    asm volatile("SAVE_STATE_VECTOR "STR(regs) ::: "x29");
#define SAVE_META(meta) \
    asm volatile("SAVE_META "STR(meta) ::: "x29", "t0");
#elif defined(__aarch64__)
#define SAVE_STATE_VECTOR(regs) \
    asm volatile("SAVE_STATE_VECTOR "STR(regs) ::: "x29");
#define SAVE_META(meta) \
    asm volatile("SAVE_META "STR(meta) ::: "x0", "x29");
#endif

void copy_file(char* src_path, char* dst_path);
char* read_file(char* path);
void prepare_result_dir(const char *dirname);
long long timestamp_us();

// Returns the number of leading equal bytes between a and b (up to n).
// Uses chunked memcmp to skip quickly over equal spans.
size_t memcmp_common_prefix(const uint8_t* a, const uint8_t* b, size_t n);

// Cross-platform helper to obtain a stable, human-friendly hostname.
// - If `override_non_null` is non-NULL and non-empty, it is copied into out.
// - Else, use `gethostname`. If it equals "localhost", try Android properties
//   and `$HOST` to construct a more descriptive name (e.g., model_serial).
// The output buffer is always NUL-terminated.
void detect_preferred_hostname(char* out, size_t out_size, const char* override_non_null);

// Android property helper used by clients and util.
// Reads property `propname` via `getprop` into `dst` (NUL-terminated).
// Returns bytes written (excluding NUL). Exits on failure.
int get_android_property(char *propname, char *dst, size_t dst_len);

#define COLOR_RED    "\033[0;31m"
#define COLOR_GREEN  "\033[0;32m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_CYAN   "\033[0;36m"
#define COLOR_RESET  "\033[0m"

// Common label alignment for diffs: align to the width of "instr_idx:"
#define LABEL_ALIGN_W (sizeof("instr_idx:") - 1)
static inline void print_aligned_label(const char* label_with_colon) {
    printf("%*s ", (int)LABEL_ALIGN_W, label_with_colon);
}

#endif // UTIL_H_
