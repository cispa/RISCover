#pragma once

#include <stdint.h>

#include "filtered_instructions.h"

#include "../fuzzing_value_map.h"
#include "../regs.h"
#include "../immediates.h"

typedef struct {
    Mnemonic mnemonic;
    uint32_t encoded;
    int field_count;
    uint32_t field_values[MAX_FIELDS];
} InstantiatedInstruction;

#include "../rng.h"

typedef struct {
    shared_rng rng;
    uint64_t   seed;
    unsigned   num_regs;
} random_difffuzz_generator;

void random_difffuzz_generator_init(random_difffuzz_generator* generator, uint64_t seed) {
    generator->seed = seed;
}

InstantiatedInstruction randomly_init_picked_instr(random_difffuzz_generator* generator, const Instruction *templ) {
    InstantiatedInstruction gen;

    gen.mnemonic = templ->mnemonic;
    gen.field_count = templ->field_count;

    uint32_t encoded = templ->value;

    shared_rng* rng = &generator->rng;

    for (unsigned i = 0; i < templ->field_count; i++) {
        const InstrField *fld = &templ->fields[i];

        /* printf("%s\n", field_type_to_str(fld->name)); */

        uint32_t chosen;
        int bits = fld->range_high - fld->range_low + 1;

        uint64_t inverted_mask = (~fld->mask & ((1<<bits)-1)) << fld->range_low;

        if (inverted_mask == 0) {
            continue;
        }

        /* switch (fld->name) { */
        /*     case FIELD_NAME_uimm4: */
        /*     case FIELD_NAME_uimm6: */
        /*         chosen = random_unsigned_immediate(rng, bits); */
        /*         goto end; */
        /*     case FIELD_NAME_imm2: */
        /*     case FIELD_NAME_imm3: */
        /*     case FIELD_NAME_imm4: */
        /*     case FIELD_NAME_imm5: */
        /*     case FIELD_NAME_imm6: */
        /*     case FIELD_NAME_imm7: */
        /*     case FIELD_NAME_imm8: */
        /*     case FIELD_NAME_imm9: */
        /*     case FIELD_NAME_imm12: */
        /*     case FIELD_NAME_imm13: */
        /*     case FIELD_NAME_imm14: */
        /*     case FIELD_NAME_imm16: */
        /*     case FIELD_NAME_imm19: */
        /*     case FIELD_NAME_imm26: */
        /*         chosen = random_signed_immediate(rng, bits); */
        /*         goto end; */
        /*     // TODO: shifted immediates */
        /*     /\* case FIELD_NAME_simm7: *\/ */
        /*     default: */
        /*         ; */
        /* } */

        /* switch (fld->name) { */
        /*     case FIELD_NAME_Rd: */
        /*     case FIELD_NAME_Rn: */
        /*     case FIELD_NAME_Rm: */

        /*     case FIELD_NAME_Vd: */
        /*     case FIELD_NAME_Vn: */
        /*     case FIELD_NAME_Vm: */
        /*     case FIELD_NAME_Vdn: */

        /*     case FIELD_NAME_Zn: */
        /*     case FIELD_NAME_Zm: */
        /*     case FIELD_NAME_Za: */
        /*     case FIELD_NAME_Zda: */
        /*     case FIELD_NAME_Zdn: */

        /*     case FIELD_NAME_Pd: */
        /*     case FIELD_NAME_Pn: */
        /*     case FIELD_NAME_Pm: */
        /*     case FIELD_NAME_Pg: */
        /*     case FIELD_NAME_PNg: */
        /*     case FIELD_NAME_PNd: */
        /*     case FIELD_NAME_Pv: */
        /*     case FIELD_NAME_PNn: */
        /*     case FIELD_NAME_Pdm: */
        /*     case FIELD_NAME_Pdn: */
        /*     case FIELD_NAME_Pt: */
        /*         /\* printf("bits %d\n", bits); *\/ */
        /*         assert(bits >= 3 && bits <= 5); */
        /*         // In 1 out of num_regs+1 cases provide random register */
        /*         if (rng_randint(rng, 0, generator->num_regs) == 0) { */
        /*             chosen = rng_next(rng); // mask below makes sure that this is fine */
        /*         } else { */
        /*             chosen = rng_next(rng) % generator->num_regs; */
        /*         } */
        /*         goto end; */
        /*     default: */
        /*         ; */
        /* } */

        // Default: just generate random bits
        chosen = rng_next(rng) % (1<<bits);

        // TODO(now): implement stuff here
        goto end;
    end:

        gen.field_values[i] = chosen;

        /* printf("setting %s to %lx\n", field_type_to_str(fld->name), chosen); */

        encoded |= ((chosen & ((1U << bits) - 1)) << fld->range_low) & inverted_mask;
    }

    gen.encoded = encoded;

    return gen;
}

InstantiatedInstruction randomly_init_instr(random_difffuzz_generator* generator) {
    int idx = rng_randint(&generator->rng, 0, instructions_count-1);
    const Instruction *templ = &instructions[idx];

    return randomly_init_picked_instr(generator, templ);
}

void fill_regs_with_fuzzing_value_map(shared_rng* rng, struct regs* regs) {
    LOOP_OVER_GP(regs,
        (void)abi_i;
        *val = fuzzing_value_map_gp_val_or_rand(rng);
    )

    // TODO(now)
    /* regs->pstate = 0; */

/* #if defined(FLOATS) || defined(VECTOR) */
/*     regs->fpsr = 0; */
/* #endif */

#if defined(FLOATS) && !defined(VECTOR)
    LOOP_OVER_FP(regs,
        (void)abi_i;
        val->u = fuzzing_value_map_fp_val_or_rand(rng);
    )
#endif

#ifdef VECTOR
    LOOP_OVER_VECTOR_REGS(regs,
        (void)abi_i;
        *(uint64_t*)val = fuzzing_value_any_val(rng);

        static_assert(sizeof(*val) % sizeof(uint64_t) == 0);
        for (unsigned i = 1; i < sizeof(*val)/sizeof(uint64_t); i++) {
            ((uint64_t*)val)[i] = fuzzing_value_map_gp_val_or_rand(rng);
        }
    )
#endif
}

void generate_input(random_difffuzz_generator* generator, uint64_t seq_num, uint32_t* instr_seq, unsigned seq_len, struct regs* regs) {
    rng_init(&generator->rng, seq_num^generator->seed);

    fill_regs_with_fuzzing_value_map(&generator->rng, regs);

    for (unsigned j = 0; j < seq_len; j++) {
        InstantiatedInstruction instr = randomly_init_instr(generator);
        instr_seq[j] = instr.encoded;
        /* printf("Instruction %2d: %-8s Encoded: 0x%08X  Fields:", */
        /*     i, mnemonic_to_str(instr.mnemonic), instr.encoded); */
        /* for (int j = 0; j < instr.field_count; j++) { */
        /*     printf(" %u", instr.field_values[j]); */
        /* } */
        /* printf("\n"); */
    }
    /* instr_seq[0] = 0x14000032; */
}
