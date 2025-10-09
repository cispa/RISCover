#pragma once

#define _GNU_SOURCE
#include <sched.h>
#undef _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CPUS 256

/*
 * cpuinfo
 */

typedef struct {
    uint32_t processor;
    uint32_t implementer;
    uint32_t architecture;
    uint32_t variant;
    uint32_t part;
    uint32_t revision;
    uint32_t midr; // constructed from the others
} cpu_info;

/* linux/arch/arm64/kernel/cpuinfo.c */
/* linux/arch/arm64/include/asm/cputype.h */
#define MIDR_IMPLEMENTOR_SHIFT    24
#define MIDR_VARIANT_SHIFT        20
#define MIDR_ARCHITECTURE_SHIFT   16
#define MIDR_PARTNUM_SHIFT         4
#define MIDR_REVISION_SHIFT        0

uint32_t build_midr(cpu_info* cpu);
unsigned parse_cpu_possible();
unsigned parse_cpuinfo(cpu_info* cpus, unsigned max_n);

/* Returns a microarchitecture name for a given MIDR or 0 if unknown */
char* midr_to_microarch(uint32_t midr);
/* Returns a vendor name for a given MIDR implementer or 0 if unknown */
char* midr_to_vendor(uint32_t midr);

/*
 * Clustering
 */

typedef struct {
    uint32_t midr;
    unsigned count;
    cpu_info* cpus[MAX_CPUS];
} core_type;

unsigned group_core_types(core_type* core_types, cpu_info* cpus, unsigned n_cpus);
int try_pin_to_cpu(int cpu);
void pin_to_cpu(int cpu);
// Verbose pin to a fixed core with green message including MIDR/vendor/micro/VEC
void pin_to_core_verbose(unsigned core);
int get_num_cpus();
unsigned pin_to_random_core();
unsigned pin_to_midr(uint32_t midr);
unsigned pin_core_type(core_type* ct);
// Try to pin to the first matching MIDR in the provided list.
// Returns the pinned core id on success; if none match, returns UINT_MAX.
unsigned pin_to_midr_list(const uint32_t* midrs, unsigned n_midrs);

// Pin to a core whose microarchitecture name matches `name` (case-insensitive).
// Returns the pinned core id. If not found, pins to a random core with warning.
unsigned pin_to_microarch_name(const char* name);

// Resolve a microarchitecture name (e.g., "Cortex-X4") to a deterministic core id.
// Returns core id on success; -1 on failure. Prints INFO on selection.
int resolve_uarch_to_core_strict(const char* name);

// Return cpu_info for the current CPU/core.
// The returned pointer is to a cached static array and remains valid.
cpu_info* get_current_cpu_info(void);
