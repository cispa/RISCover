__attribute__((section(".gitcommit"))) const char git_commit[] = "0000000000000000000000000000000000000000";

#include <stdio.h>

#include "lib/rng.h"
#include "lib/regs.h"
#include "lib/runner.h"

#include "lib/randomdifffuzzgenerator.h"

int main(int argc, char** argv) {
    struct regs regs;
    assert(argc == 4);
    uint64_t seed = atoi(argv[1]);
    uint64_t seq_num = atoi(argv[2]);
    uint64_t seq_len = atoi(argv[3]);

    random_difffuzz_generator generator;
    random_difffuzz_generator_init(&generator, seed);

    uint32_t instr_seq[seq_len];
    generate_input(&generator, seq_num, (uint32_t*)&instr_seq, seq_len, &regs);

    log_input(stdout, (uint32_t*)&instr_seq, seq_len, &regs, 0, 0);
}
