#!/usr/bin/env python3
import os

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

import pyutils.config as config

from pyutils.disassembly import disasm_capstone
from pyutils.inp import InputWithSparseValues
from pyutils.server_util import clients_from_client_tuples, real_clients_from_client_tuples, cluster_clients_by_results
from pyutils.repro import Repro
from pyutils.server import Server, group_clients_by_args, execute_inputs_on_clients
from pyutils.util import client_to_header_colored

def main():
    ###################### Shared Logic Part ################################

    from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed, parser_add_common_extension_parsing, get_collection_from_args

    parser = get_common_argparser()

    parser.add_argument('--until', type=int)
    parser.add_argument('--no-check-flags', action='store_true')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--instructions', nargs='+')
    group.add_argument('--repros', nargs='+')
    # TODO: fix arg parsing, maybe with subcommands?
    parser.add_argument('--reg', action='append', default=[], help='Specify register values in the format "aX=Y"')

    parser_add_common_extension_parsing(parser)

    args = parse_args(parser)
    flags, non_repro_flags = parse_flags(args)

    seq_len=1
    if args.repros:
        repros = [Repro.from_file(repro) for repro in args.repros]
        seq_len=max(map(lambda r: len(r.inp.instr_seq), repros))

    non_repro_flags |= set([f"-DMAX_SEQ_LEN={seq_len}", "-DWITH_FULL_REGS"])

    print_infos_if_needed(args, flags.union(non_repro_flags), files=args.repros or [])

    instruction_collection = get_collection_from_args(args)

    ##########################################################################

    # TODO: add args so that we can launch multiple instructions with regs from cli
    # e.g. --instruction 0x... t6=0x0 ft11=0
    # val = 0x0000003000003000
    if args.instructions:
        inputs = [InputWithSparseValues.from_args(args=args, instr_seq=[int(i, 16)]) for i in args.instructions]
    else:
        inputs = []
        for repro in repros:
            if not args.no_check_flags:
                if repro.flags != flags:
                    print(
f"""You are running with different flags than specified in repro {repro}
Repro flags:   {repro.flags}
Current flags: {flags}
You can ignore this warning with --no-check-flags. But be super careful with this."""
                    )
                    exit(1)
            inputs += [repro.inp]

    inputs = [inp.to_input_with_values() for inp in inputs]

    if config.VECTOR:
        # vector inputs are bigger
        batches_of = 500
    else:
        batches_of = 2000

    # Execute inputs
    server = Server(None, args.port, args.expect, batches_of, single_client=args.single_client)
    server.start()
    server.join()
    clients = group_clients_by_args(args, server.clients)

    print(f"All clients connected. Now executing {len(inputs)} inputs.")

    for i in range(0, len(inputs), batches_of):
        batch = inputs[i:i+batches_of]

        results_per_client = execute_inputs_on_clients(batch, clients)
        results_per_input = list(map(list, zip(*results_per_client)))

        for inp, results in zip(batch, results_per_input):
            result_to_clients = cluster_clients_by_results(results)
            for instr in inp.instr_seq:
                d_capstone = "\n".join(disasm_capstone([instr]))
                dis_mra = instruction_collection.disassemble(instr)
                print(f"0x{instr:08x}\tcap: {d_capstone},\tdis_mra: {dis_mra}")
                instruction_collection.pretty_print_instr(instr)
                print()
            print()
            initial_regs = inp.decode_regs()
            for result, client_tuples in result_to_clients.items():
                result.initial_regs = initial_regs
                print(", ".join(map(client_to_header_colored, real_clients_from_client_tuples(client_tuples))))
                print(result)
                print()

            print("---------------------------------------------------------------------------")
            print()

    print("Finished")
    os._exit(0)

if __name__ == '__main__':
    main()
