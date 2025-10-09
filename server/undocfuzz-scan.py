#!/usr/bin/env python3
import signal
import sys
import os
import shutil
from multiprocessing import Value
import signal

from pyutils.shared_logic import parse_and_set_flags, get_collection_from_args, parser_add_common_extension_parsing
parse_and_set_flags()

import pyutils.config as config
from pyutils.inp import Input
from pyutils.util import ILL_ILLOPC, sec_to_str
from pyutils.result import Result
from pyutils.server_util import clients_from_client_tuples, real_clients_from_client_tuples, cluster_clients_by_results
from pyutils.server import Server, Client, execute_inputs_on_clients, group_clients_by_args, RealClient
from pyutils.fuzzserver import Worker, FuzzServer
from pyutils.repro import Repro
from pyutils.generation.generators.undocgenerator import UndocGenerator

# TODO: fill half of addresses with segv
# TODO: skip immediates?

output_dir = "undocfuzz-scan"
repro_dir = os.path.join(output_dir, "reproducers")
progress_file_path = os.path.join(output_dir, "progress")

# TODO: disable sending back regs
# TODO: remove counter in stats here

# TODO: 0x638c40 hangs qemu-v4 v5, in reproducer looks like sigill currently

batches_of = 10000

class UndocFuzzWorker(Worker):
    # TODO: better would be to have UndocFuzzServer here but how to do cyclic references?
    def __init__(self, fuzz_server: FuzzServer, clients: list[Client], n: int, generator, step: int, until: int):
        super().__init__(fuzz_server, clients, n)
        self.step = step
        self.until = until
        self.generator = generator

        self.progress_file = open(progress_file_path, "a")
        self.client_file_mapping = { client: open(os.path.join(output_dir, client.hostname+"-"+client.microarchitecture.model_name), "a") for client in clients }

    def run(self):
        while True:
            # Get the next to generate counter
            with self.fuzz_server.condition:
                if self.fuzz_server.counter.value >= self.until:
                    self.fuzz_server.finished.value = True
                    break
                to_generate = self.fuzz_server.counter.value
                self.fuzz_server.counter.value = min(self.fuzz_server.counter.value+self.step, self.until)

            # NOTE: we pass until here so that we can parallelize
            # otherwise we would have to wait for the generator until its known where
            # one can continue
            inputs = self.generator.generate(to_generate, min(to_generate+self.step, self.until))

            if config.VERBOSE:
                for inp in inputs:
                    self.fuzz_server.print(f"generated: 0x{inp.instr_seq[0]:08x}")

            # inputs can be empty if all instructions in the range are documented
            if inputs:
                results_per_client = execute_inputs_on_clients(inputs, self.clients)
                results_per_input = list(map(list, zip(*results_per_client)))
            else:
                results_per_input = []

            # Wait for our time to analyze the results
            with self.fuzz_server.condition:
                self.fuzz_server.condition.wait_for(lambda: self.fuzz_server.analysed_counter.value == to_generate)

            for inp, results in zip(inputs, results_per_input):
                self.analysis(inp, results)

            # Only increment after we analyzed so that logs are in the correct order
            with self.fuzz_server.condition:
                self.fuzz_server.executed_counter.value += len(inputs)
                self.fuzz_server.analysed_counter.value += self.step
                self.fuzz_server.condition.notify_all()

    def analysis(self, inp: Input, results: list[Result]):
        logged = False

        instr = inp.instr_seq[0]

        for result in results:
            if result.signum != signal.SIGILL or result.si_code != ILL_ILLOPC:
                self.client_file_mapping[result.client].write(f"0x{instr:08x}\n")
                logged = True

        if logged:
            result_to_clients = cluster_clients_by_results(results)

            inp = inp.to_input_with_values()
            initial_regs = inp.decode_regs()
            for result in result_to_clients.keys():
                result.initial_regs = initial_regs

            repro = Repro(inp, self.fuzz_server.flags, result_to_clients=result_to_clients, generator=self.generator, counter=instr)
            repro_path = os.path.join(repro_dir, f"0x{instr:08x}.yaml")

            with open(repro_path, "w") as f:
                f.write(repro.to_yaml_with_comments(self.generator.instruction_collection))

        if config.VERBOSE:
            self.fuzz_server.print("executed", hex(instr))

        cur = instr // 0xffff
        with self.fuzz_server.condition:
            if logged or cur != self.fuzz_server.last_logged.value:
                # TODO: why not logged?
                self.fuzz_server.last_logged.value = cur
                self.progress_file.write(f"0x{instr:08x}\n")


class UndocFuzzServer(FuzzServer):
    def __init__(self, server: Server, clients: list[Client], step: int, start_at: int, generator, until: int, single_step: bool, flags):
        super().__init__(server, clients, (generator, step, until), UndocFuzzWorker, single_step=single_step, start_counter_at=start_at)

        self.until = until
        self.start_at = start_at
        self.flags = flags

        self.counter = Value('Q', start_at)
        self.analysed_counter = Value('Q', start_at)
        self.last_logged = Value('Q', 0)

    def stats(self, elapsed_seconds: int) -> list[tuple[str, str]]:
        with self.condition:
            analysed_counter = self.analysed_counter.value
            executed_counter = self.executed_counter.value

        analyzed_diff = analysed_counter-self.start_at
        if analyzed_diff != 0 and elapsed_seconds != 0:
            instr_per_sec = analyzed_diff//elapsed_seconds
            remaining = (self.until-analysed_counter)//instr_per_sec

            return [
                ("Scanned/s", str(instr_per_sec)),
                ("Remain", sec_to_str(remaining)),
                ("Skipped", f"{int((1-executed_counter/analysed_counter)*100)}%"),
                ("At", f"0x{analysed_counter:08x}")
            ]
        return []


def main():
    global args, batches_of, filter_func

    ###################### Shared Logic Part ################################

    from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed

    parser = get_common_argparser()
    parser_add_common_extension_parsing(parser)

    parser.add_argument('--step', type=int, help="Enables single stepping (no other batches in flight). Use for debugging.")
    parser.add_argument('--until', type=int)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--filter', action='store_true')

    args = parse_args(parser)
    flags, non_repro_flags = parse_flags(args)

    non_repro_flags.add("-DMAX_SEQ_LEN=1")

    print_infos_if_needed(args, flags.union(non_repro_flags))

    ##########################################################################

    if os.path.exists(output_dir):
        if not args.resume:
            alt=output_dir+"-old"
            if os.path.exists(alt):
                shutil.rmtree(alt)
                print(f"Deleted old results dir {alt}")
            shutil.move(output_dir, alt)
            print(f"Moved old results folder to {alt}")
            os.mkdir(output_dir)
    else:
        os.mkdir(output_dir)

    if not args.resume:
        os.mkdir(repro_dir)

    filter_func = None
    match config.ARCH:
        case "riscv64":
            if args.filter:
                filter_func = lambda i: filter_broken_thead(i, ["x29", "t0"])

    assert(args.group_by == "hostname+microarch")
    # If we store the results then it makes sense to also record the mem change
    assert(args.check_mem)

    collection = get_collection_from_args(args)
    generator = UndocGenerator(instruction_collection=collection, instruction_filter=filter_func)

    if args.step:
        # Don't keep packets in flight
        single_step = True
        step = args.step
    else:
        single_step = False
        # Infer batch size from maximum in flight bytes
        # At least 3 batches should be in flight
        # TODO: this can fail if no invalid instruction in the first 1000
        step = RealClient.max_in_flight_bytes//(3*len(generator.generate(counter=0, until=1000)[0].pack()))
    print(f"Sending out batches of step={step} inputs.")

    if args.resume:
        start_at = int(open(progress_file_path, "r").readlines()[-1].strip(), 16)
        print(f"Resuming at {hex(start_at)} (loaded from progress file)")
    else:
        start_at = 0
        print(f"Starting at {hex(start_at)}")

    until = 1 << 32
    if args.until:
        until = args.until

    server = Server(None, port=args.port, expect=args.expect, max_batch_n=step, single_step=single_step, single_client=args.single_client)
    server.start()
    server.join()

    clients = group_clients_by_args(args, server.clients)
    fuzz_server = UndocFuzzServer(server=server, clients=clients, step=step, start_at=start_at, \
                                  generator=generator, until=until, single_step=single_step, flags=flags)

    fuzz_server.start()

    print()
    print("Finished scan succesfully")

if __name__ == '__main__':
    main()
