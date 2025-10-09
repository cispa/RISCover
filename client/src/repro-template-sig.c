// FLAGS
// ARCH

__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

#include <stdio.h>
#include <time.h>

#include "lib/runner.h"
#include "lib/util.h"
#include "lib/log.h"
#include "lib/musl_heap_base.h"
#include "lib/cpuinfo.h"
#include "lib/maps.h"

MAPPINGS
unsigned mappings_n = asizeof(mappings);

uint32_t orig_instr_seq[] = {INSTR_SEQ};
uint32_t* target_seq;
unsigned  target_seq_n;

// NOTE: Uncomment here to use the assembled instructions
// instead of the original bytes.
// #define USE_ASSEMBLED

// NOTE: Uncomment here if you want to force
// usage of the assembled bytes
// #define FORCE_ASSEMBLED

#ifdef USE_ASSEMBLED
extern uint32_t assembled_instr_seq[];
extern uint32_t assembled_instr_seq_end[];

asm(
    ".global assembled_instr_seq\n"
    ".global assembled_instr_seq_end\n"
    "assembled_instr_seq:\n\t"
INSTR_DIS
    "assembled_instr_seq_end:\n\t"
);
#endif

int cpu_core = -1;
// Preferred MIDR list to try
static uint32_t midrs[] = MIDRS;
static unsigned midrs_n = asizeof(midrs);
// If provided via CLI, override with a single MIDR
static uint32_t midr_cli_override = 0;
// If provided via CLI, override with microarch name
static char cli_uarch[128] = {0};

static void print_usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [core N | midr 0x.... | uarch NAME]\n\n"
            "Arguments:\n"
            "  core N       Use logical core index N (decimal).\n"
            "  midr 0x....  Use cores with this MIDR (hex or decimal).\n"
            "  uarch NAME   Use microarch by name (e.g., Cortex-X4).\n\n"
            "Note: specify exactly one of core, midr, or uarch (mutually exclusive).\n\n"
            "Examples:\n"
            "  %s\n"
            "  %s core 3\n"
            "  %s midr 0x4108d821\n"
            "  %s uarch Cortex-X4\n",
            argv0, argv0, argv0, argv0, argv0);
}

__attribute__((noreturn)) void run() {
    struct regs regs;
REGS

    repro_run_common(&regs, target_seq, target_seq_n,
                     (struct mapping*)&mappings, mappings_n,
                     cpu_core, midrs, midrs_n,
                     midr_cli_override,
                     (cli_uarch[0] ? cli_uarch : NULL));

    exit(0);
}

int main(int argc, char** argv) {
    early_init();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "core") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing value for 'core'.");
                print_usage(argv[0]);
                return 1;
            }
            cpu_core = atoi(argv[++i]);
        } else if (strcmp(argv[i], "midr") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing value for 'midr'.");
                print_usage(argv[0]);
                return 1;
            }
            midr_cli_override = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "uarch") == 0) {
            if (i + 1 >= argc) {
                log_error("Missing value for 'uarch'.");
                print_usage(argv[0]);
                return 1;
            }
            strncpy(cli_uarch, argv[++i], sizeof(cli_uarch)-1);
            cli_uarch[sizeof(cli_uarch)-1] = '\0';
        } else {
            log_error("Unknown or malformed argument near '%s'.", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Exclusivity: allow at most one of core, midr, uarch
    int specified = (cpu_core >= 0) + (midr_cli_override != 0) + (cli_uarch[0] != '\0');
    if (specified > 1) {
        log_error("Specify only one of 'core', 'midr', or 'uarch'.");
        print_usage(argv[0]);
        return 1;
    }

    if (cpu_core >= 0) {
        log_info("Selected core %d", cpu_core);
    } else if (midr_cli_override != 0) {
        log_info("Selected MIDR 0x%x", midr_cli_override);
    } else if (cli_uarch[0]) {
        log_info("Selected uarch '%s'", cli_uarch);
    } else {
        log_info("No pinning preference provided; will use template defaults or random core");
    }

#ifdef USE_ASSEMBLED
    target_seq = assembled_instr_seq;
    target_seq_n = assembled_instr_seq_end-assembled_instr_seq;

  #ifndef FORCE_ASSEMBLED
    if (memcmp(target_seq, orig_instr_seq, sizeof(orig_instr_seq)) != 0) {
        log_warning(
               "Compiled instruction sequence is not the same. Probably disassembly is messed up or missing an instruction.\n"
               "You can ignore this warning, but we will use the original instruction sequence not the assembled one.\n"
               "You can define FORCE_ASSEMBLED to skip this check to use the assembled sequence.");
        printf("original  sequence: ");
        print_hexbuf((uint32_t*)orig_instr_seq, asizeof(orig_instr_seq));
        printf("assembled sequence: ");
        print_hexbuf((uint32_t*)target_seq, target_seq_n);
        printf("\n");

        target_seq = orig_instr_seq;
        target_seq_n = asizeof(orig_instr_seq);
    }
  #endif
#else
   target_seq = orig_instr_seq;
   target_seq_n = asizeof(orig_instr_seq);
#endif

    // For picking a random core
    srand(time(NULL));

    clean_memory_mappings();
    switch_stack(STACK_ADDR, &run);
}

MAPPING_VALUES
