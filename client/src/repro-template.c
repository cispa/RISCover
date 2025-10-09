// FLAGS
// ARCH

__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

// NOTE: Uncomment here if you want to force
// usage of the assembled bytes
// #define FORCE_ASSEMBLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/regs.h"
#include "lib/util.h"
#include "lib/log.h"

struct regs regs;
struct regs _regs_result;
struct regs _regs_restore;

extern uint32_t instr_seq[];
uint32_t orig_instr_seq[] = {INSTR_SEQ};

static void print_usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [--help]\n\n"
            "This template does not accept arguments. Pin cores manually via taskset if needed.\n",
            argv0);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            log_error("Unknown argument: %s", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    log_warning("This template does not pin the CPU core automatically. Pin via taskset if needed.");

    #ifndef FORCE_ASSEMBLED
    if (memcmp(instr_seq, orig_instr_seq, sizeof(orig_instr_seq)) != 0) {
        log_warning("Compiled instruction sequence is not the same. Probably disassembly is messed up or missing an instruction.\n"
                "If that is the case, use the signal variant (remove --no-sig) and follow the guide there.\n");
        print_hexbuf((uint32_t*)orig_instr_seq, asizeof(orig_instr_seq));
        print_hexbuf((uint32_t*)instr_seq, asizeof(orig_instr_seq));
        exit(EXIT_FAILURE);
    }
    #endif

REGS

    INCLUDE_REG_MACROS();
    SAVE_STATE_EXTENDED(_regs_restore);
    RESTORE_STATE_EXTENDED(regs);

    asm volatile(
        ".global instr_seq\n"
        "instr_seq:\n\t"
INSTR_DIS
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
    );

    SAVE_STATE_EXTENDED(_regs_result);
    RESTORE_STATE_EXTENDED(_regs_restore);

    print_reg_diff(&regs, &_regs_result);

    return 0;
}
