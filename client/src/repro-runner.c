__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#include "lib/runner.h"
#include "lib/util.h"
#include "lib/log.h"
#include "lib/maps.h"
#include "lib/cpuinfo.h"


static char yaml_path[1024];
static int   cli_core = INT32_MIN;   // INT32_MIN means "unset"
static uint32_t cli_midr = 0;        // 0 means "unset"
static char  cli_uarch[128] = {0};   // empty means "unset"

static void print_usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s <reproducer.yaml> [core N | midr 0x.... | uarch NAME]\n"
            "\n"
            "Arguments:\n"
            "  <reproducer.yaml>   Path to YAML reproducer file.\n"
            "  core N              Use logical core index N (decimal).\n"
            "  midr 0x....         Use cores with MIDR value (hex or decimal).\n"
            "  uarch NAME          Use microarch by name (e.g., Cortex-X4).\n"
            "\n"
            "Note: specify exactly one of core, midr, or uarch (mutually exclusive).\n"
            "\n"
            "Examples:\n"
            "  %s repro.yaml\n"
            "  %s repro.yaml core 3\n"
            "  %s repro.yaml midr 0x4108d821\n"
            "  %s repro.yaml uarch Cortex-X4\n",
            argv0, argv0, argv0, argv0, argv0);
}

__attribute__((noreturn)) void run() {
    struct regs regs = {0};
    uint32_t instr_seq[MAX_SEQ_LEN] = {0};
    unsigned seq_n = 0;
    struct mapping* mappings = NULL;
    unsigned mappings_n = 0;
    uint32_t* yaml_midrs = NULL;
    unsigned yaml_midrs_n = 0;

    load_repro_from_file(yaml_path, &regs, (uint32_t*)&instr_seq, &seq_n,
                         &mappings, &mappings_n,
                         &yaml_midrs, &yaml_midrs_n);

    repro_run_common(&regs, (uint32_t*)&instr_seq, seq_n,
                     (struct mapping*)mappings, mappings_n,
                     cli_core, (yaml_midrs_n ? yaml_midrs : NULL), yaml_midrs_n,
                     cli_midr,
                     (cli_uarch[0] ? cli_uarch : NULL));

    exit(0);
}

int main(int argc, char** argv) {
    early_init();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // First positional arg is YAML path; remaining optional key-value pairs
    strncpy(yaml_path, argv[1], sizeof(yaml_path) - 1);
    yaml_path[sizeof(yaml_path) - 1] = '\0';

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    size_t len = strlen(yaml_path);
    if (len < 5 || strcmp(&yaml_path[len-5], ".yaml") != 0) {
        log_error("Input must be a .yaml file.");
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "core") == 0) {
            if (i + 1 >= argc) { log_error("Missing value for 'core'."); print_usage(argv[0]); return 1; }
            cli_core = atoi(argv[++i]);
        } else if (strcmp(argv[i], "midr") == 0) {
            if (i + 1 >= argc) { log_error("Missing value for 'midr'."); print_usage(argv[0]); return 1; }
            cli_midr = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "uarch") == 0) {
            if (i + 1 >= argc) { log_error("Missing value for 'uarch'."); print_usage(argv[0]); return 1; }
            strncpy(cli_uarch, argv[++i], sizeof(cli_uarch)-1);
            cli_uarch[sizeof(cli_uarch)-1] = '\0';
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            log_error("Unknown or malformed argument near '%s'.", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Exclusivity: allow at most one of core, midr, uarch
    int specified = (cli_core != INT32_MIN) + (cli_midr != 0) + (cli_uarch[0] != '\0');
    if (specified > 1) {
        log_error("Specify only one of 'core', 'midr', or 'uarch'.");
        print_usage(argv[0]);
        return 1;
    }

    if (cli_core != INT32_MIN) {
        log_info("Selected core %d", cli_core);
    } else if (cli_midr != 0) {
        log_info("Selected MIDR 0x%x", cli_midr);
    } else if (cli_uarch[0]) {
        log_info("Selected uarch '%s'", cli_uarch);
    } else {
        log_info("No pinning preference provided; will use YAML or random core");
    }

    // For picking a random core
    srand(time(NULL));

    clean_memory_mappings();
    switch_stack(STACK_ADDR, &run);
}
