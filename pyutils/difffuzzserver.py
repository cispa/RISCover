#!/usr/bin/env python3
import os
import yaml
from multiprocessing import Value
import signal
from abc import ABC, abstractmethod
from typing import Callable
from itertools import combinations, chain

import pyutils.config as config

# TODO: depending on arch
from pyutils.inp import Input, InputWithRegSelect, InputWithValues, InputJustSeqNum
from pyutils.util import bcolors, color_str, create_results_folder, client_to_header, signal_powerset, hex_yaml, PAGE_SIZE, gp
from pyutils.result import Result, FilteredResult, LenientResult, MultiResult
from pyutils.server_util import clients_from_client_tuples, real_clients_from_client_tuples, cluster_clients_by_results
from pyutils.repro import Repro
from pyutils.server import Server, RealClient, Client, execute_inputs_on_clients, execute_input_on_clients, LostClientException
from pyutils.fuzzserver import Worker, FuzzServer
from pyutils.generation.generators.randomdifffuzzgenerator import Generator, DiffFuzzGenerator
from pyutils.generation.instruction import Instruction
from pyutils.qemu import LocalQemu

output_dir="results"

# This function removes unusable results from a clustered results and returns a new dict.
# It should be called whenever len(cluster_clients_by_results) is used to decide
# if to log something.
# NOTE: This is also called on full (non-stepping) sequences so it should make sure that
# that can not become an issue. For example, in general, we should not remove results just because
# one result is SIGILL. Cluster first, then remove.
def cluster_remove_unusable(inp: InputWithRegSelect, result_to_clients: dict[Result, list[tuple[Client, RealClient]]]) -> int:

    new = {}

    # If there is only one cluster with SIGILL, remove that.
    # If there are none, do nothing.
    # If there are are more than one, we found something.
    sigill=0
    for res in result_to_clients.keys():
        if res.signum == signal.SIGILL:
            sigill += 1

    # Craft new dict
    for res, v in result_to_clients.items():
        if sigill == 1:
            # We can just filter out all with SIGILL here as there is only one class.
            if res.signum == signal.SIGILL:
                continue

        new[res] = v

    return new

def wrap_filters_single(res: Result, inp: Input, filt) -> FilteredResult:
    return FilteredResult(LenientResult(res), inp, filt)

def wrap_filters(results: list[Result], inp: Input, filt) -> list[FilteredResult]:
    return list(map(lambda res: wrap_filters_single(res, inp, filt), results))

def find_minimal_diff_tree(inp: Input, until_seq_len: int, get_results_for_seq_len: Callable[[list[list[Client]], int], tuple[list[Result], list[FilteredResult]]], clients: list[Client]) -> list[tuple[dict[Result, list[Client]], int]]:
    current_classes: list[list[Client]] = [clients]

    splits = []
    for seq_len in range(1, until_seq_len+1):
        results, filtered_results = get_results_for_seq_len(list(chain(*current_classes)), seq_len)

        result_by_client = {}
        filtered_result_by_client = {}
        for result, filtered_result in zip(results, filtered_results):
            result_by_client[result.client] = result
            filtered_result_by_client[result.client] = filtered_result

        new_classes = []
        for clients in current_classes:
            results = [result_by_client[client] for client in clients]
            result_to_clients = cluster_clients_by_results(results)

            # Sort by the length of the clients list (majority vote which should be the main result)
            result_to_clients = dict(sorted(result_to_clients.items(), key=lambda x: len(x[1]), reverse=True))

            # Don't filter result_to_clients as we need still need that unfiltered.
            usable_clusters = cluster_remove_unusable(inp, result_to_clients)

            # No need to filter anything when normal results already not match.
            if len(usable_clusters) > 1:
                filtered_results = [filtered_result_by_client[client] for client in clients]
                filtered_result_to_clients = cluster_remove_unusable(inp, cluster_clients_by_results(filtered_results))

                # Cluster by the filtered results, but then log the real results
                if len(filtered_result_to_clients) > 1:
                    splits += [(result_to_clients, seq_len)]
                else:
                    # Still continue as there might be changes later
                    # in the sequence.
                    pass

            # Also here, use the real results for going further
            for result, client_tuples in result_to_clients.items():
                if result.signum == 0:
                    if len(client_tuples) > 1:
                                        # Pass on non-real clients
                        new_classes += [clients_from_client_tuples(client_tuples)]
                    else:
                        # There is only one client left in this class
                        pass
                else:
                    # There was a signal, so don't continue to execute the sequence
                    pass

        current_classes = new_classes

        if len(current_classes) == 0:
            break

    inp.seq_len = until_seq_len
    return splits

def multiresults_to_results_per_seq_len(multiresults: list[MultiResult], max_seq_len: int) -> list[list[Result]]:
    results_per_seq_len = []
    for i in range(max_seq_len):
        results_this_seq_len = []
        for multiresult in multiresults:
            if i < len(multiresult.results):
                results_this_seq_len += [multiresult.results[i]]
        results_per_seq_len += [results_this_seq_len]
    return results_per_seq_len

# def minimize_diff(inp: InputWithRegSelect, res1: Result, res2: Result, filt):
#         prev_res1 = res1
#         prev_res2 = res2
#         orig_instr_seq = inp.instr_seq
#         nn = None
#         for n_remove in range(1, len(orig_instr_seq)):
#             inp.instr_seq = orig_instr_seq[:-n_remove]+n_remove*[0x1f2003d5]
#             new_res1, new_res2 = execute_input_on_clients(inp, [res1.client, res2.client])
#             new_res1, new_res2 = FilteredResult(new_res1, inp, filt), FilteredResult(new_res2, inp, filt)
#             new_diffs = new_res1.diff(new_res2)
#             if not new_diffs:
#                 nn = n_remove - 1
#                 break
#             else:
#                 nn = n_remove
#             prev_res1 = new_res1
#             prev_res2 = new_res2

#         if nn != 0:
#             new_seq = orig_instr_seq[:-nn]
#         else:
#             new_seq = orig_instr_seq
#         inp.instr_seq = orig_instr_seq

#         return new_seq, prev_res1, prev_res2

class DiffFuzzWorker(Worker,ABC):
    # TODO: better would be to have DiffFuzzServer here but how to do cyclic references?
    def __init__(self, fuzz_server: FuzzServer, clients: list[Client], n: int, instruction_collection, batch_size: int, generator: DiffFuzzGenerator):
        super().__init__(fuzz_server, clients, n)
        self.instruction_collection = instruction_collection
        self.batch_size = batch_size

        # TODO: there are probably other places where a data structure is synchronized
        # maybe instruction_collection?
        # also this is not optimal, do something else
        # maybe we should just make the fuzzing map static and just dump it once from the client
        # or just make it global
        self.generator = generator

    def run(self):
        clients_path = os.path.join(output_dir, "clients.yaml")
        with open(clients_path, "w") as f:
            f.write(hex_yaml([client.to_dict() for client in self.clients]))

        use_qemu = False
        # if config.ARCH == "riscv64":
        #     use_qemu = False
        if use_qemu:
            self.qemu = LocalQemu(self.generator.seed)
            # If this fails, you did not copy over the (correct) client to FRAMEWORK_ROOT on the server.
            assert(self.clients[0].elf_hash.value == self.qemu.client.elf_hash.value)

        while True:
            # Get the next to generate counter
            with self.fuzz_server.condition:
                to_generate = self.fuzz_server.counter.value
                self.fuzz_server.counter.value += self.batch_size

            # print(self.n, "now genning: ", to_generate)
            inputs = list(self.generator.generate(to_generate, self.batch_size))

            # NOTE: This assumes that all inputs have the same seq length.
            # Otherwise we would have to deepcopy inputs or create a light wrapper around them.
            # For now this is easiest.
            seq_len = inputs[0].seq_len
            assert(all(inp.seq_len == seq_len for inp in inputs))

            if config.VERBOSE:
                self.fuzz_server.print(f"generated {len(inputs)}:")
                for j, inp in enumerate(inputs):
                    self.fuzz_server.print("counter:", to_generate+j)
                    # inp.print(self.instruction_collection)
                    self.fuzz_server.print()

            # TODO: currently there is no way to notice if worker deadlocks

            # If client disconnects, try again as long as clients are left
            while len(self.clients) >= 2 or (len(self.clients) == 1 and use_qemu):
                in_analysis = False
                try:
                    for inp in inputs:
                        inp.full_seq = True

                    results_per_client = execute_inputs_on_clients(inputs, self.clients)

                    in_analysis = True

                    # Expand compressed inputs. E.g. for offline seq gen we send seq_num + n which expands to seq_num + 0, seq_num + 1, ..., seq_num + n here.
                    if self.generator.expand_inputs_after_exec:
                        inputs = list(next(self.generator.generate(to_generate+i, 1)) for i in range(self.batch_size))
                        for inp in inputs:
                            inp.full_seq = True

                    filtered_results_per_client = [(MultiResult(wrap_filters(multiresult.results, inp, self.filters)) for multiresult, inp in zip(_results, inputs)) for _results in results_per_client]
                    results_per_input = list(map(list, zip(*results_per_client)))
                    filtered_results_per_input = list(map(list, zip(*filtered_results_per_client)))

                    # Add QEMU if it helps and is enabled.
                    # TODO: we could massively speed this up by using collecting inputs first and then
                    # running all. But that does not work with qemu as single inputs can crash.
                    # So for now this mode is just slow.
                    if use_qemu:
                        for j, (inp, multiresults, filtered_multiresults) in enumerate(zip(inputs, results_per_input, filtered_results_per_input)):
                            results_last_seq = [multiresult.results[-1] for multiresult in multiresults]
                            clusters = cluster_clients_by_results(results_last_seq)
                            clusters_cleaned = cluster_remove_unusable(inp, clusters)
                            removed = len(clusters)-len(clusters_cleaned)

                            # Add QEMU if there is only one client or if there is no diff and we removed classes.
                            # E.g. when SIGILL vs OK
                            if (len(clusters_cleaned) == 1 and removed > 0) or len(results_last_seq) == 1:
                                try:
                                    qemu_multiresult = execute_input_on_clients(inp, [self.qemu], priority=True)[0]
                                    qemu_filtered_multiresult = MultiResult(wrap_filters(qemu_multiresult.results, inp, self.filters))

                                    multiresults += [qemu_multiresult]
                                    filtered_multiresults += [qemu_filtered_multiresult]
                                except LostClientException as e:
                                    # Just ignore qemu then
                                    pass

                    # Calculate diff trees.
                    diffs = []
                    for j, (inp, multiresults, filtered_multiresults) in enumerate(zip(inputs, results_per_input, filtered_results_per_input)):
                        # Since we are differentially fuzzing there should be at least two clients.
                        # This can be QEMU + another client.
                        assert(len(multiresults) >= 2)

                        results_per_seq_len = multiresults_to_results_per_seq_len(multiresults, seq_len)
                        filtered_results_per_seq_len = multiresults_to_results_per_seq_len(filtered_multiresults, seq_len)

                        clients = list(map(lambda res: res.client, results_per_seq_len[0]))
                        def get_results_for_seq_len(_clients: list[Client], _seq_len: int) -> tuple[list[Result], list[FilteredResult]]:
                            return results_per_seq_len[_seq_len-1], filtered_results_per_seq_len[_seq_len-1]
                        diff_tree: list[tuple[dict[Result, list[Client]], int]] = find_minimal_diff_tree(inp, seq_len, get_results_for_seq_len, clients)

                        if diff_tree:
                            diffs += [(inp, diff_tree, j)]

                    # And now log the discovered diffs.
                    for inp, diff_tree, j in diffs:
                        # Get the full input
                        full_input = inp.to_input_with_values()

                        # Log all the splits individually
                        for result_to_clients, _seq_len in diff_tree:

                            # We will log this, so add QEMU as reference if not used already.
                            if use_qemu and not self.qemu in (v[0] for l in result_to_clients.values() for v in l):
                                try:
                                    inp.seq_len = _seq_len
                                    inp.full_seq = False
                                    qemu_result = execute_input_on_clients(inp, [self.qemu])[0]
                                    if qemu_result in result_to_clients:
                                        result_to_clients[qemu_result] += [(qemu_result.client, qemu_result.real_client)]
                                    else:
                                        result_to_clients[qemu_result]  = [(qemu_result.client, qemu_result.real_client)]
                                except LostClientException as e:
                                    # Just ignore qemu then
                                    pass

                            full_input.seq_len = _seq_len
                            self.log_repro(to_generate+j, full_input, result_to_clients)

                    break
                except LostClientException as e:
                    if not e.unrelated:
                        # We don't want to print anything when we probably didn't crash the fuzzer
                        # NOTE: this can print multiple times because we merge inputs in the server
                        self.fuzz_server.print(color_str(f"Worker {self.n:3} lost client {e.real_client.hostname} (Core {e.real_client.n_core+1}/{e.real_client.num_cpus}) at {to_generate:9}{' during analysis' if in_analysis else ''}:", bcolors.FAIL) + f" {e.orig_exception}")
                    elif self.n == 0:
                        # Let worker 0 always print sot hat we always at least have the message once
                        self.fuzz_server.print(color_str(f"Worker {self.n:3} lost client {e.real_client.hostname} (Core {e.real_client.n_core+1}/{e.real_client.num_cpus}) at {to_generate:9}{' during analysis' if in_analysis else ''}{' but unrelated' if e.unrelated else ''}:", bcolors.FAIL) + f" {e.orig_exception}")
                    if e.remove_client:
                        self.clients.remove(e.remove_client)

                    # Reset inp attributes to default because we modify them.
                    # Otherwise we would need deepcopy which is slow.
                    for inp in inputs:
                        inp.full_seq = False
                        inp.seq_len = seq_len
            else:
                # TODO: quit when all workers exited
                self.fuzz_server.print(color_str(f"Quitting worker {self.n:3} because remaining clients {len(self.clients)} < 2 at counter {to_generate:9}.", bcolors.WARNING))
                break

            with self.fuzz_server.condition:
                self.fuzz_server.executed_counter.value += len(inputs)

    def log_repro(self, counter: int, inp: InputWithValues, result_to_clients: dict[Result, list[tuple[Client, RealClient]]]):
        initial_regs = inp.decode_regs()

        # TODO: think of how to nicely do this
        # should we just always expand this during gen?
        for result in result_to_clients.keys():
            result.initial_regs = initial_regs

        with self.fuzz_server.condition:
            repro_n = self.fuzz_server.reproducer_counter.value + 1
            self.fuzz_server.reproducer_counter.value += 1

        repro = Repro(inp, self.fuzz_server.flags, result_to_clients=result_to_clients, generator=self.generator, counter=counter)

        diffs = repro.all_diffs()
        assert(diffs)

        repro_path = os.path.join(os.path.join(output_dir, "reproducers"), f"reproducer-{repro_n:08}-{counter:012}.yaml")

        with open(repro_path, "w") as f:
            f.write(repro.to_yaml_with_comments(self.instruction_collection))

        # for clearing the old status line
        self.fuzz_server.print(f"New reproducer: {repro_path}")

        # Around 1GB
        if repro_n > 300000:
            self.fuzz_server.print("Exiting because of too many files...")
            os._exit(0)

    def custom_filters(self, inp: InputWithRegSelect, res1: Result, res2: Result, filter_bidirectional: Callable[[Callable[[Result, Result], bool], Callable[[], bool]], bool]) -> bool:
        return False

    def filters(self, inp: InputWithRegSelect, res1: Result, res2: Result):
        # Apply custom filters coming from custom class
        def filter_bidirectional(diff_filter: Callable[[Result, Result], bool], general_filter: Callable[[], bool]) -> bool:
            return (diff_filter(res1, res2) or diff_filter(res2, res1)) and general_filter()
        if self.custom_filters(inp, res1, res2, filter_bidirectional):
            return True


class SimpleDiffFuzzWorker(DiffFuzzWorker):
    def __init__(self, fuzz_server: FuzzServer, clients: list[Client], n: int, instruction_collection, batch_size: int, generator):
        super().__init__(fuzz_server, clients, n, instruction_collection, batch_size=batch_size, generator=generator)

    # Returning true means the diff is invalid/can be skipped
    def custom_filters(self, inp: InputWithRegSelect, res1: Result, res2: Result, filter_bidirectional: Callable[[Callable[[Result, Result], bool], Callable[[], bool]], bool]) -> bool:
        match config.ARCH:
            case "riscv64":
                return self.custom_filters_riscv64(inp, res1, res2, filter_bidirectional)
            case "aarch64":
                return self.custom_filters_aarch64(inp, res1, res2, filter_bidirectional)

    def custom_filters_riscv64(self, inp: InputWithRegSelect, res1: Result, res2: Result, filter_bidirectional: Callable[[Callable[[Result, Result], bool], Callable[[], bool]], bool]) -> bool:
        diffs = res1.diff(res2)

        if filter_bidirectional(lambda res1, res2: True, lambda: diffs in signal_powerset(["pstate", "si_addr", "si_pc", "si_code"])):
            return True

        return False

    def custom_filters_aarch64(self, inp: InputWithRegSelect, res1: Result, res2: Result, filter_bidirectional: Callable[[Callable[[Result, Result], bool], Callable[[], bool]], bool]) -> bool:
        # TODO: not sure what we should do about ok vs sigbus. Hiding them is wrong. Probably just leave them and then cluster?
        if filter_bidirectional(lambda res1, res2: res1.orig_signum == signal.SIGBUS and res2.orig_signum == 0, lambda: True):
            return True

        # Any filters based on differing signum should be above, everything else below
        # if config.META:
        #     if filter_bidirectional(lambda res1, res2: res1.cycle_diff*100 < res2.cycle_diff, lambda: True):
        #         log_repro(num, batch, basename, othername, baseres, otherres, diffs)
        #         return True

        diffs = res1.diff(res2)

        # glibc/sysdeps/unix/sysv/linux/bits/siginfo-consts.h
        # "ILL_ILLOPC = 1,		/* Illegal opcode.  */",
        # "ILL_ILLOPN,			/* Illegal operand.  */",
        # "ILL_ILLADR,			/* Illegal addressing mode.  */",
        # "ILL_ILLTRP,			/* Illegal trap. */",
        # "ILL_PRVOPC,			/* Privileged opcode.  */",
        # "ILL_PRVREG,			/* Privileged register.  */",
        # "ILL_COPROC,			/* Coprocessor error.  */",
        # "ILL_BADSTK,			/* Internal stack error.  */",
        # "ILL_BADIADDR			/* Unimplemented instruction address.  */",

        # TODO: si_code also differs sometimes for segfaults between lab27 lab55 (1 vs 2)
        if filter_bidirectional(lambda res1, res2: True, lambda: diffs in signal_powerset(["pstate", "si_addr", "si_pc", "si_code"])):
            return True

        # ARM segfaulting page boundary crossing stores and loads can introduce
        # non-determinism on regs and memory. This filters most of it on memory.
        if res1.mem_diffs != None and diffs and res1.signum == signal.SIGSEGV and res2.signum == signal.SIGSEGV and diffs == set(["mem"]):
            matches = True
            diffs1 = set(res1.mem_diffs or [])
            diffs2 = set(res2.mem_diffs or [])
            for start, n, val, checksum in diffs1.symmetric_difference(diffs2):
                # if (start+n) % PAGE_SIZE != 0:
                          # The (hopefully) maximum size of store instructions (stp 16B vector)
                if (start+32) // PAGE_SIZE == start // PAGE_SIZE:
                    matches = False
                    break
            if matches:
                return True

        # # a lot of si_addr differences on apple because of pointer auth
        # if filter_bidirectional(lambda res1, res2: res1.signum == signal.SIGSEGV, lambda: diffs in signal_powerset(["si_addr"])):
        #     return True

        return False


class DiffFuzzServer(FuzzServer):
    def __init__(self, server: Server, clients: list[Client], batch_size: int, start_at: int, instruction_collection, flags: list[str], generator, single_step=False):
        super().__init__(server, clients, (instruction_collection, batch_size, generator), SimpleDiffFuzzWorker, single_step=single_step, start_counter_at=start_at)

        self.counter = Value('Q', start_at)
        self.reproducer_counter = Value('Q', 0)
        self.flags = flags
        self.generator = generator

    def start(self):
        create_results_folder(output_dir)
        os.mkdir(os.path.join(output_dir, "reproducers"))

        super().start()

    def stats(self, _: int) -> list[tuple[str, str]]:
        with self.condition:
            return [("Reproducers", str(self.reproducer_counter.value))]
