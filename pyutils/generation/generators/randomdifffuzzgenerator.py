#!/usr/bin/env python3
if __name__ == '__main__':
    import sys, os
    sys.path.append(os.getenv("FRAMEWORK_ROOT"))




import sys
from pyutils.inp import Input, InputWithRegSelect
import random
import numpy as np
from pyutils.util import VEC_REG_SIZE, gp, fp, vec
import pyutils.config as config
from pyutils.generation.generator import DiffFuzzGenerator, Generator
from pyutils.shared_logic import get_collection_from_args
from pyutils.generation.generationutil import seed_all

match config.ARCH:
    case "riscv64":
        from pyutils.riscv.fuzzing_value_map import fuzzing_value_map_gp, fuzzing_value_map_fp
    case "aarch64":
        from pyutils.arm.fuzzing_value_map import fuzzing_value_map_gp, fuzzing_value_map_fp

# TODO: doing it like this was around twice as fast but made the code more complex
# def gen_new_inputs(instruction_collection, n, fuzzing_value_map_gp, fuzzing_value_map_fp, seq_len, num_regs, seed):
#     seed_all(seed)
#     gp_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(gp)*n)
#     fp_select=np.random.randint(0, len(fuzzing_value_map_fp), size=len(fp)*n)
#     match config.ARCH:
#         case "riscv64":
#             fcsr_select=np.random.randint(0, 0b111, size=n)
#             instr_choices = np.random.randint(0, len(instruction_collection.instructions), size=n*seq_len)
#         case "aarch64":
#             instr_choices = np.random.randint(0, len(instruction_collection.instructions), size=n*seq_len)
#     match config.ARCH:
#         case "riscv64":
#             if config.VECTOR:
#                 vec_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(vec)*n*VEC_REG_SIZE//8)
#                 return [InputWithRegSelect(gp_select[g:g+len(gp)], fp_select[f:f+len(fp)], [instruction_collection.init_instr(instruction_collection.instructions[instr_choices[j]], num_regs)
#                         for j in range(i, i+seq_len)], int(fcsr_select[fscr_i]), reg_select_vec=vec_select[v:v+len(vec)*VEC_REG_SIZE//8]) for g, f, v, i, fscr_i in zip(range(0, len(gp)*n, len(gp)), range(0, len(fp)*n, len(fp)), range(0, len(vec)*n*VEC_REG_SIZE//8, len(vec)*VEC_REG_SIZE//8), range(0, n*seq_len, seq_len), range(n))]
#             else:
#                 return [InputWithRegSelect(gp_select[g:g+len(gp)], fp_select[f:f+len(fp)], [instruction_collection.init_instr(instruction_collection.instructions[instr_choices[j]], num_regs)
#                         for j in range(i, i+seq_len)], int(fcsr_select[fscr_i])) for g, f, i, fscr_i in zip(range(0, len(gp)*n, len(gp)), range(0, len(fp)*n, len(fp)), range(0, n*seq_len, seq_len), range(n))]
#         case "aarch64":
#             if config.VECTOR:
#                 vec_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(vec)*n*VEC_REG_SIZE//8)
#                 return [InputWithRegSelect(gp_select[g:g+len(gp)], None, [instruction_collection.init_instr(None, num_regs, mnemonic=list(instruction_collection.instructions.keys())[instr_choices[j]])
#                         for j in range(i, i+seq_len)], int(0), reg_select_vec=vec_select[v:v+len(vec)*VEC_REG_SIZE//8]) for g, f, v, i in zip(range(0, len(gp)*n, len(gp)), range(0, len(vec)*n*VEC_REG_SIZE//8, len(vec)*VEC_REG_SIZE//8), range(0, n*seq_len, seq_len), range(n))]
#             else:
#                 return [InputWithRegSelect(gp_select[g:g+len(gp)], fp_select[f:f+len(fp)], [instruction_collection.init_instr(None, num_regs, mnemonic=list(instruction_collection.instructions.keys())[instr_choices[j]])
#                         for j in range(i, i+seq_len)], int(0)) for g, f, i, fscr_i in zip(range(0, len(gp)*n, len(gp)), range(0, len(fp)*n, len(fp)), range(0, n*seq_len, seq_len), range(n))]

def gen_new_input(instruction_collection, seq_len, num_regs, seed, weighted) -> InputWithRegSelect:
    seed_all(seed)
    gp_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(gp))

    if weighted:
        instr_choices = random.choices(list(instruction_collection.weighted_instructions_cum.keys()), cum_weights=list(instruction_collection.weighted_instructions_cum.values()), k=seq_len)
    else:
        # TODO: it was around twice as fast when we used indices here and used them later
        instr_choices = random.choices(list(instruction_collection.instructions.keys()), k=seq_len)

    match config.ARCH:
        case "riscv64":
            if config.VECTOR or config.FLOATS:
                fp_select=np.random.randint(0, len(fuzzing_value_map_fp), size=len(fp))
            if config.VECTOR:
                vec_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(vec)*VEC_REG_SIZE//8)
                return InputWithRegSelect(gp_select, [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs).pack()
                        for j in instr_choices], fp_select, reg_select_vec=vec_select)
            elif config.FLOATS:
                return InputWithRegSelect(gp_select, [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs).pack()
                        for j in instr_choices], fp_select)
            else:
                return InputWithRegSelect(gp_select, [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs).pack()
                        for j in instr_choices])
        case "aarch64":
            if config.VECTOR:
                vec_select=np.random.randint(0, len(fuzzing_value_map_gp), size=len(vec)*VEC_REG_SIZE//8)
                instrs = [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs) for j in instr_choices]
                return InputWithRegSelect(gp_select, [i.pack() for i in instrs], reg_select_vec=vec_select, instructions=instrs)
            elif config.FLOATS:
                fp_select=np.random.randint(0, len(fuzzing_value_map_fp), size=len(fp))
                return InputWithRegSelect(gp_select, [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs).pack()
                        for j in instr_choices], reg_select_fp=fp_select)
            else:
                return InputWithRegSelect(gp_select, [instruction_collection.randomly_init_instr(mnemonic=j, num_regs=num_regs).pack()
                        for j in instr_choices])

def gen_new_instr_sequentially(n, last):
    return list(range(last, last+n))

def remove_instructions(collection):
    # # jumps are just bad
    # # and we had the issue that a loop like this:
    # # 000000000007c000 <orig_instr_seq>:
    # # 7c000:       8f11                    sub     a4,a4,a2
    # # 7c002:       0001                    nop
    # # 7c004:       bff5                    j       7c000 <orig_instr_seq>
    # # 7c006:       0001                    no
    # # resulted in different a4 because of speed -> many false positives
    # # TODO: fuzz them too?
    # # NOTE: but we want branches -> so for now also ignore alarm
    # remove_instructions = ["ecall", "scall", "fence_tso", "fence", "c_j", "c_jal", "c_jalr", "c_jr", "jal", "jalr"]

    # ############# T-Head crash #############
    #                         # NOTE: these are excluded because T-Head chips allow writes to these 3 bits
    # remove_instructions += ["csrrw", "csrrc", "csrrwi", "csrrsi", "csrrci"]
    # # extensions          += ["rv_zicsr", "custom/rv_thead"]
    # ########################################

    # # NOTE: vsetvli introduces a lot of differences
    # # dont exclude it because it's probably needed for interesting diffs

    # if args.standard_extensions:
    #     args.extensions = ["rv32_c", "rv32_c_f", "rv32_i", "rv64_a", "rv64_c", "rv64_d", "rv64_f", "rv64_i", "rv64_m", "rv64_q", "rv_a", "rv_c", "rv_c_d", "rv_d", "rv_f", "rv_i", "rv_m", "rv_q"]

    # if args.extensions:
    #     if not args.add_extensions:
    #         args.add_extensions = []
    #     print("Using extensions:", args.extensions+args.add_extensions)
    #     rvop = RvOpcodesDis(args.extensions+args.add_extensions, remove_instructions=remove_instructions)
    # else:
    #     print("Using all extensions as defined in RvOpcodesDis")
    #     if args.add_extensions:
    #         print("Plus the following added extensions:", args.add_extensions)
    #         rvop = RvOpcodesDis(remove_instructions=remove_instructions, add_extensions=args.add_extensions)
    #     else:
    #         rvop = RvOpcodesDis(remove_instructions=remove_instructions)

    match config.ARCH:
        case "riscv64":
            # from list_unstable_instructions.py
            remove_instructions = ["rdcycle", "rdtime", "rdinstret"]

            # remove other known bad instructions
            remove_instructions += ["ecall", "scall"]

            remove_instructions += ["csrrw", "csrrc", "csrrwi", "csrrsi", "csrrci"]

            # TODO: add a filter, this is again the 3 bit thing
            remove_instructions += ["fscsr", "frflags"]
            pass
        case "aarch64":
            # TODO: at some point infer those with this list unstable script
            # grep -h -R A reproducers | sort | uniq
            # NOTE: these all generate different values on M1 and M2
            remove_instructions = [
                "A64.control.branch_reg.RETAA_64E_branch_reg",
                # TODO: why hints?
                "A64.control.hints.PACIA1716_HI_hints",
                "A64.control.hints.PACIB1716_HI_hints",
                "A64.control.hints.XPACLRI_HI_hints",
                "A64.dpreg.dp_1src.PACDA_64P_dp_1src",
                "A64.dpreg.dp_1src.PACDB_64P_dp_1src",
                "A64.dpreg.dp_1src.PACIA_64P_dp_1src",
                "A64.dpreg.dp_1src.PACIB_64P_dp_1src",
                "A64.dpreg.dp_2src.PACGA_64P_dp_2src",
            ]
            # TODO: pacibsp, pacibz, paciasp, pacib1716 (A64.control.hints.HINT_HM_hints) is not documented?
            # TODO: these only generate a change on M1?
            # repros even sigill on m2
            remove_instructions += [
                "A64.control.hints.AUTIA1716_HI_hints",
                "A64.control.hints.AUTIB1716_HI_hints",
                "A64.dpreg.dp_1src.AUTDA_64P_dp_1src",
                "A64.dpreg.dp_1src.AUTDB_64P_dp_1src",
                "A64.dpreg.dp_1src.AUTIA_64P_dp_1src",
                "A64.dpreg.dp_1src.AUTIB_64P_dp_1src",
                "A64.control.branch_reg.BLRAAZ_64_branch_reg",
            ]

            # TODO: remove
            # they sigill on lab27, lab08
            # and on lab11 and lab55 they also produce a diff (si_addr)
            # we cant filter that because its a combined diff (signum (vs lab08) + si_addr (vs lab11))
            # both would filter individually but not like that
            remove_instructions += [
                "A64.ldst.ldst_pac.LDRAA_64_ldst_pac",
            ]

            # TODO: these are the pac instructions
            # maybe we have the wrong specification?
            # find a spec where they disassemble correctly
            # like that we shadow other instructions
            # TODO: the problem is that these are shadowed by the others removed above (e.g. A64.control.hints.PACIA1716_HI_hints)
            # maybe removing them is the best like we remove undef?
            remove_instructions += [ "A64.control.hints.HINT_HM_hints" ]

            # syscalls
            remove_instructions += [ "A64.control.exception.SVC_EX_exception" ]

            # msr
            remove_instructions += [
                "A64.control.systemmove.MRS_RS_systemmove",
                "A64.control.systemmove.MSR_SR_systemmove",
                "A64.control.systemmovepr.MRRS_RS_systemmovepr",
                "A64.control.systemmovepr.MSRR_SR_systemmovepr",
            ]

            # These introduce diffs on Kunpeng Pro vs Pixel 9 because si_addr
            # differs.
            # Performance impact with direct full seq is not that high so enabled.
            # But gives false positives.
            # emit the SIGILL on different addresses.
            # remove_instructions += [
            #     "MOVPRFX-Z.Z-_",
            #     "MOVPRFX-Z.P.Z-_",
            # ]

    collection.remove_instructions(remove_instructions)

                                                                      # ugly fix so that this is not printed for --print-flags
    print("Removed the following instructions:", remove_instructions, file=sys.stderr)

class RandomDiffFuzzGenerator(DiffFuzzGenerator):
    def __init__(self, instruction_collection, seq_len, num_regs, seed, weighted, compress_send=True):
        super().__init__(instruction_collection=instruction_collection, compress_send=compress_send)

        self.seq_len = seq_len
        self.num_regs = num_regs
        self.seed = seed
        self.weighted = weighted

    @classmethod
    def from_args(cls, args):
        collection = get_collection_from_args(args)
        remove_instructions(collection)
        return cls(instruction_collection=collection, seq_len=args.seq_len, num_regs=args.num_regs, seed=args.seed, weighted=args.weighted)

    # TODO: add regs parameter here and pass to compilation (how many regs are restored/saved)
    # thats faster, since we dont every operate on the rest
    # but does this make sense? If we have side effects?
    # side effects are no problem, we can still send back everything, just not set everything, but save all after
    def generate(self, counter: int, n: int) -> list[InputWithRegSelect]:

        # TODO: if sequence generation is too slow, we still have the old code that is around twice as fast

        # TODO: args.filter_thead does nothing
        # if not any(filter_broken_thead(x, time_to_bug=args.time_to_bug) for x in b.instr_seq):

        # TODO: test if cores match
        # icestorm first 4 cores on lab11?
        # probably just spin something and observe differences between the cores

        # TODO: check folder lab25:lab55_difffuzz_false_positives
        # start_clients.sh --port 1400 --stay-attached --max-threads lab25 lab55 -- server/diffuzz-server.py --floats --vector --seq-len 5
        # seem to be loads from a low memory region, maybe the lscpu output, maybe we should just clear that
        # for that we need to implement run_on again

        # TODO: test if the middle one is fast enough
        # inputs = [gen_new_input_weighted(self.instruction_collection, self.seq_len, self.num_regs, (counter+i)^self.seed) for i in range(n)]
        return (gen_new_input(self.instruction_collection, self.seq_len, self.num_regs, (counter+i)^self.seed, self.weighted) for i in range(n))
        # inputs = [gen_new_inputs(instruction_collection, 1, seq_len, 32, (counter+i)^seed)[0] for i in range(n)]
        # NOTE: this one is faster but dosent work with get-input-as-repro or skip-to as seed is wrong. This needs a more involved implementation that is currently removed.
        # inputs = gen_new_inputs(instruction_collection, n, seq_len, 32, counter^seed)

    def to_dict(self) -> dict:
        d = super().to_dict()
        return d | {
            "seq_len": self.seq_len,
            "num_regs": self.num_regs,
            "seed": self.seed,
            "weighted": self.weighted,
        }

    def parser_add_args(parser):
        parser.add_argument('--seq-len', type=int, default=5)
        parser.add_argument('--num-regs', type=int, default=5)
        parser.add_argument('--weighted', action='store_true')
        parser.add_argument('--seed', type=int, default=0)

    def get_build_flags(self) -> tuple[set, set]:
        flags, non_repro_flags = super().get_build_flags()
        non_repro_flags = non_repro_flags | {f"-DMAX_SEQ_LEN={self.seq_len}", "-DWITH_REGS"}
        return flags, non_repro_flags


# test if instructions still assemble
def main():
    import argparse

    from pyutils.shared_logic import parse_and_set_flags
    parse_and_set_flags()

    from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_most_common_argparser

    parser = get_most_common_argparser()
    parser_add_common_extension_parsing(parser)
    RandomDiffFuzzGenerator.parser_add_args(parser)

    args = parser.parse_args()
    generator = RandomDiffFuzzGenerator.from_args(args)
    collection = generator.instruction_collection

    print(next(generator.generate(0, 1)).to_input_with_values().to_yaml(instruction_collection=collection))

    from pyutils.disassembly import disasm_opcodes
    with open(f'{os.getenv("FRAMEWORK_ROOT")}/misc/generation/ambigious_instructions.txt', "r") as ambigue_f:
        with open(f'{os.getenv("FRAMEWORK_ROOT")}/misc/generation/non_dis_instructions.txt', "r") as non_dis_f:
            ambigue = set(ambigue_f.read().split("\n"))
            non_dis = set(non_dis_f.read().split("\n"))

    with open(f'{os.getenv("FRAMEWORK_ROOT")}/misc/generation/ambigious_instructions.txt', "w") as ambigue_f:
        with open(f'{os.getenv("FRAMEWORK_ROOT")}/misc/generation/non_dis_instructions.txt', "w") as non_dis_f:
            for i, instr in collection.instructions.items():
                i_int = collection.randomly_init_instr(i, 10)
                if i != collection.disassemble(i_int):
                    # print(i, collection.disassemble(i_int))
                    if not i in ambigue:
                        ambigue.add(i)
                opcodes = disasm_opcodes([i_int])
                if not opcodes or any(map(lambda a: "undefined" in a, opcodes)):
                    if i not in non_dis:
                        non_dis.add(i)
                    # if "AA" in i:
                    #     continue
                    # if "SYS" in i:
                    #     continue
                    # assert(False)
                    # if i in
                    # if op
                # assert(opcodes and not any(map(lambda a: "undefined" in a, opcodes)))
                # assert()

            ambigue_f.write("\n".join(sorted(ambigue)))
            non_dis_f.write("\n".join(sorted(non_dis)))


if __name__ == '__main__':
    main()
