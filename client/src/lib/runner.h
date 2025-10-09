#pragma once

#include <setjmp.h>
#include <stdint.h>
#include <unistd.h>

#include "regs.h"
#include "cpuinfo.h"

struct mapping {
    uint64_t start;
    size_t n;
    int prot;
    uint8_t* val;
    uint8_t* rw_mapping;
    int fd;
};

// CHECK_MEM_CUT_AT: number of bytes recorded per mem change.
// Reason: 8 - 4 - 4 = 16, so next twos complement is +16 = 32
// NOTE: Update in result.py as well if updated!
#define CHECK_MEM_CUT_AT 16
struct mem_change {
    uint64_t start;
    // len of mem change
    uint32_t n;
    uint8_t  val[CHECK_MEM_CUT_AT];
    // FNV-1a 32-bit checksum over the entire changed range
    // (used to detect diffs beyond the first CHECK_MEM_CUT_AT bytes)
    uint32_t checksum;
};
static_assert(sizeof(struct mem_change) == 32);

struct result {
    int signum;
    // TODO: why cant we use si_addr here?
    uint64_t si_addrr;
    uint64_t si_pc;
    // 1-based index of the faulting instruction within runner_code_start..end; 0 if unknown/not in range
    uint32_t instr_idx;
    // TODO: only si_code for sigill?
    int si_code;
    struct regs regs_result;
    #ifdef META
    struct meta meta;
    #endif
    #ifdef CHECK_MEM
    uint8_t n_mem_changes;
    struct mem_change mem_changes[CHECK_MEM_MAX_NUMBER_MEM_CHANGES];
    #endif /* CHECK_MEM */
};

struct result run_with_instr(const uint32_t instr, struct regs* regs_before);
struct result run_with_instr_seq(const uint32_t* instr, unsigned n, struct mapping* mappings, unsigned mappings_n, struct regs* regs_before);
unsigned run_with_instr_seq_full_seq(const uint32_t* instr, unsigned l, struct mapping* mappings, unsigned mappings_n, struct result* results, struct regs* regs_before);
int mem_change_equal(const struct mem_change* m1, const struct mem_change* m2);
int result_equal(const struct result* r1, const struct result* r2);
// Print result to stdout with colors enabled
void print_result(struct regs* regs_before, struct result* r);
// Configurable printer: choose FILE, line prefix and color control
void print_result_opt(FILE* file, const char* prefix, const struct regs* regs_before, struct result* r, int color_on);
void clean_memory_mappings();

void runner_init();

void log_input(FILE* file, uint32_t* instr_seq, unsigned n, const struct regs *regs_before, struct mapping* mappings, unsigned mappings_n);

// Pair a result with the CPU it was produced on
typedef struct {
    struct result* result;
    cpu_info* cpu;
} result_cpu_pair;

// Print a diff between two results to FILE with optional line prefix and color control
void print_result_diff(FILE *file, struct result* a, struct result* b, const char* prefix, int color_on);

// YAML logger for a full reproducer. Prepend a human-readable commented section.
void log_repro(FILE *file, result_cpu_pair* items, unsigned n_items, uint32_t* instr_seq, unsigned n, const struct regs *regs_before, struct mapping* mappings, unsigned mappings_n);
// Result logger with optional line prefix for nested YAML blocks.
void log_result(FILE *file, struct result* result, const struct regs *regs_before, const char* prefix, const cpu_info* cpu);

// Log current device information as a standalone YAML document.
// Starts with top-level `hostname`, then `microarchitectures` list.
void log_client_info_yaml(FILE* file);

// Cached vector register size (bytes) query; computed on first call.
int get_vec_size(void);

// Build optional vector size suffix used in logs, e.g., " VEC=16".
// Writes an empty string when vectors are not enabled.
void build_vec_suffix(char* out, size_t out_size);

// Load a YAML repro into regs, instruction sequence, mappings and optional CPU pinning info.
// Dynamically allocates and grows `mappings` via realloc.
void load_repro(char* content,
                struct regs* regs_before,
                uint32_t* instr_seq,
                unsigned* seq_n,
                struct mapping** mappings,
                unsigned* mappings_n,
                uint32_t** midrs_out,
                unsigned* midrs_out_n);

// Convenience: load repro directly from YAML file path.
void load_repro_from_file(const char* path,
                          struct regs* regs_before,
                          uint32_t* instr_seq,
                          unsigned* seq_n,
                          struct mapping** mappings,
                          unsigned* mappings_n,
                          uint32_t** midrs_out,
                          unsigned* midrs_out_n);

void setup_memory_mapping(struct mapping* mapping);
void setup_memory_mappings(struct mapping* mappings, unsigned n);
void load_memory_mapping(struct mapping* mapping);
void load_memory_mappings(struct mapping* mappings, unsigned n);
void free_memory_mapping(struct mapping* mapping);
void free_memory_mappings(struct mapping* mappings, unsigned n);
struct mapping* find_mapping(struct mapping* mappings, unsigned n, uintptr_t addr);

// Common helper to pin to a CPU, initialize the runner, prepare mappings,
// execute an instruction sequence, and print the result using the provided
// initial registers snapshot.
// - regs_before: initial register state used for diff when printing
// - instr_seq/seq_n: instruction bytes to execute
// - mappings/mappings_n: preconfigured memory mappings to set up and load
// - core: preferred CPU core (>=0), or negative to pick random core
// - midrs/midrs_n: optional list of MIDRs to try (order matters)
// - midr_override: if non-zero, pin by this MIDR (takes precedence over list)
void repro_run_common(struct regs* regs_before,
                      uint32_t* instr_seq,
                      unsigned seq_n,
                      struct mapping* mappings,
                      unsigned mappings_n,
                      int core,
                      const uint32_t* midrs,
                      unsigned midrs_n,
                      uint32_t midr_override,
                      const char* uarch_override);

void common_pin(int core,
                const uint32_t* midrs,
                unsigned midrs_n,
                uint32_t midr_override,
                const char* uarch_override);

// Early process init: unbuffered stdout/stderr, set musl heap base,
// and print detected CPU microarchitectures.
void early_init();
