#!/usr/bin/env python3
import argparse
import signal
import tabulate
import os
import yaml
import sys
import shutil
import random
import pickle
import numpy as np
from time import time, sleep
from multiprocessing import Value

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

# rg --files-without-match 'ALRM' ~/diffuzz-framework/results/reproducers/reproducer-* | xargs bat
# rg --files-without-match segv | xargs rg --files-without-match sigbus | xargs bat --style=header --line-range :50

import pyutils.config as config

from pyutils.generation.generators.allgenerators import parser_add_generator_flags, get_generator_from_args
from pyutils.server import RealClient, Server, group_clients_by_args
from pyutils.repro import Repro
from pyutils.difffuzzserver import DiffFuzzServer
from pyutils.qemu import LocalQemu

# How to debug crashes:
#
# NOTE: for all of these you probably also want to use --single-client as otherwise crashes
# can be non-deterministic.
# NOTE: if a bug does not want to reproduce make sure that you use the full extensions.
# E.g. use --vector on machines that have the vector extensions. That can otherwise
# introduce non-determinism since the vector registers are not cleared when not using --vector.
#
# 1. Test if reproducible:
#    --skip-to <last-counter-you-saw>
# 2. Find the coarse location:
#    --skip-to <last-counter-you-saw> --step 1000 (or higher)
#    then another run with e.g. 50
#    --skip-to <last-counter-you-saw> --step 50 (or higher)
# 2. Then find the exact location
#    --skip-to <last-counter-you-saw> --step 1 --verbose
# 3. Finally get the reproducers (can be executed on your local machine):
#    <same-args> --get-input-as-repro <the-counter-you-found>
#
# If the bug does not reproduce while searching the location it's likely that
# multiple inputs cause the crash. Then you can try to find the other input as well.
# Try that with binary search via --skip-to and big --step.
# Afterwards try to find the input that then triggers the crash with a small --step but remember
# to not change the --skip-to you found before.

# TODO: write a mode that just does crash fuzzing, no error reporting, no sending back register results

def main():
    ###################### Shared Logic Part ################################

    from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed, parser_add_common_extension_parsing, get_collection_from_args

    parser = get_common_argparser()

    parser_add_generator_flags(parser)

    # TODO: reimplement
    # parser.add_argument('--until', type=int)
    parser.add_argument('--step', type=int, help="Enables single stepping (no other batches in flight). Use for debugging.")
    # NOTE: Only use the values that the fuzzer spits out in a normal run
    parser.add_argument('--skip-to', type=int)
    parser.add_argument('--filter-thead', action='store_true')
    parser.add_argument('--might-crash', action='store_true')
    parser.add_argument('--get-input-as-repro', type=int)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--seq-benchmark', action='store_true')
    parser.add_argument('--time-to-bug', action='store_true')

    parser_add_common_extension_parsing(parser)

    args = parse_args(parser)

    if args.seq_benchmark:
        args.benchmark = True
        config.META = True

    flags, non_repro_flags = parse_flags(args)

    generator = get_generator_from_args(args)
    generator_flags, generator_non_repro_flags = generator.get_build_flags()
    flags |= generator_flags
    non_repro_flags |= generator_non_repro_flags

    all_flags = flags.union(non_repro_flags)

    generator.early_init(all_flags)

    print_infos_if_needed(args, all_flags)

    generator.late_init(all_flags)

    ##########################################################################

    if args.get_input_as_repro != None:
        counter = args.get_input_as_repro
        inp = next(generator.generate(counter, 1))

        repro = Repro(inp.to_input_with_values(), flags, generator=generator, counter=counter)
        out = repro.to_yaml(generator.instruction_collection)
        print()
        print(out)
        path = f"repro_{counter}.yaml"
        with open(path, "w") as f:
            f.write(out)
        print(f"Wrote repro to file {path}")
        os._exit(0)

    if args.step:
        # Don't keep packets in flight
        single_step = True
        step = args.step
    else:
        single_step = False
        # Infer batch size from maximum in flight bytes
        # At least 3 batches should be in flight
        # step = RealClient.max_in_flight_bytes//(3*len(next(generator.generate(counter=0, n=1)).pack()))
        # TODO(now)
        step = 500
    print(f"Sending out batches of step={step} inputs.")

    if args.skip_to:
        start_at = args.skip_to
    else:
        start_at = 0

    # TODO: replace all the sys.exit and exit with os exit or make a wrapper shutdown_fuzz

    server = Server(None, args.port, args.expect, step, compress_send=generator.compress_send, single_step=single_step, single_client=args.single_client, seed=generator.seed)
    server.start()
    server.join()

    print(f"All clients connected. Now starting differential fuzzing.")

    clients = group_clients_by_args(args, server.clients)
    fuzz_server = DiffFuzzServer(server, clients, step, start_at, generator.instruction_collection, flags, generator, single_step=single_step)

    fuzz_server.start()

if __name__ == '__main__':
    main()
