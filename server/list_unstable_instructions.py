#!/usr/bin/env python3
import time
import os

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

import pyutils.config as config

from pyutils.inp import InputWithSparseValues, InputWithValuesRiscv64, InputWithValuesAarch64, expand_regs_fp, expand_regs_gp, expand_regs_vec
from pyutils.shared_logic import get_collection_from_args, parser_add_common_extension_parsing
from pyutils.util import bcolors, color_str
from pyutils.server import Server, group_clients_by_args, execute_inputs_on_clients

def main():
    ###################### Shared Logic Part ################################

    from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed

    parser = get_common_argparser()

    parser_add_common_extension_parsing(parser)

    args = parse_args(parser)
    flags, non_repro_flags = parse_flags(args)

    flags |= non_repro_flags
    flags |= set([f"-DMAX_SEQ_LEN=1", "-DWITH_FULL_REGS"])

    print_infos_if_needed(args, flags.union(non_repro_flags))

    ##########################################################################

    if not config.FLOATS or not config.VECTOR:
        print()
        print(color_str("Warning: You are running without --floats or --vector."))
        print(color_str("Make sure to understand that this can exclude certain instructions that e.g. modify vector or floating regs. Best case use a machine with a lot of extensions for this experiment."))
        print()

    # NOTE: on riscv we don't find rdcycleh because it's sigill on rv64

    instruction_collection = get_collection_from_args(args)
    # TODO: a lot are illegal, fix that somehow, maybe brute force a few bits and try to get them non-sigill
    mnemonics = list(instruction_collection.instructions)
    instructions = []
    for m in mnemonics:
        instr = instruction_collection.assemble_static_fields(m)
        match config.ARCH:
            case "riscv64":
                # NOTE: don't use x29 (t4) here because that is our scratch
                # NOTE: it is important to pass mnemonic here since otherwise rdcycle is rewritten to be some other csrrs instruction
                instr = instruction_collection.instr_set_encoding(instr,    "rd", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,   "rs1", 2, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,   "rs2", 3, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,   "rs3", 4, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,  "imm2", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,  "imm3", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,  "imm4", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,  "imm5", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr,  "imm6", 1, no_match_fine=True, mnemonic=m)
                instr = instruction_collection.instr_set_encoding(instr, "imm12", 1, no_match_fine=True, mnemonic=m)
            case "aarch64":
                # TODO
                exit(1)
        instructions += [instr]

    if config.ARCH == "aarch64":
        # TODO: for pacga we need to restart fuzzer
        # maybe write a command for restart and reconnect?
        # but the server cant handle this
        # just fork and kill parent?
        instructions += [0x9ac83219]
        mnemonics += ["pactest"]

    server = Server(None, args.port, args.expect, 10000)
    server.start()
    server.join()
    clients = group_clients_by_args(args, server.clients)

    iterations = 5
    print(f"All clients connected. Now executing {len(instructions)} instructions {iterations} times on each host.")

    inputs = [InputWithSparseValues(instr_seq=[i]).to_input_with_values() for i in instructions]

    # 5 iterations of executing each of these instructions
    instruction_results_per_client = [[[] for _ in instructions] for _ in clients]
    for i in range(iterations):
        results_per_client = execute_inputs_on_clients(inputs, clients)
        for results, new_results in zip(instruction_results_per_client, results_per_client):
            for result_list, result in zip(results, new_results):
                result_list += [result]

        print(f"Iteration {i+1}/{iterations} done.")

        # NOTE: we had a sleep here before so that we can find changes that depend on time
        # or other counters but we probably don't need that since we spend enough time
        # sending out and collecting everything synchronously

    # Check if any of these 3 instruction results differed (seperately for each machine)
    unstable = set()
    for results in instruction_results_per_client:
        for instruction_results, mnemonic, inp in zip(results, mnemonics, inputs):
            # Transitively diff
            assert(len(instruction_results) == iterations)
            res1 = instruction_results[0]
            for res2 in instruction_results[1:]:
                diffs = res1.diff(res2)
                if diffs:
                    if config.VERBOSE:
                        print(f"Found a diff on mnemonic {mnemonic}")
                        print("Diffs:", diffs)
                        # TODO: probably give input to result? so that it can then get the initial_regs?
                        initial_regs = inp.decode_regs()
                        res1.initial_regs = initial_regs
                        res2.initial_regs = initial_regs
                        print(res1.client.microarchitecture.model_name)
                        print(res1)
                        print(res2.client.microarchitecture.model_name)
                        print(res2)
                        print()
                    unstable.add(mnemonic)
                    break

    if not unstable:
        print(color_str(f"Finished. No unstable instructions found in {iterations} iterations.", color=bcolors.FAIL))
    else:
        print(color_str(f"Finished. Found {len(unstable)} unstable instructions in {iterations} iterations:", color=bcolors.OKGREEN))
        for i in sorted(unstable):
            print(color_str(i, color=bcolors.OKCYAN))

    os._exit(0)

if __name__ == '__main__':
    main()
