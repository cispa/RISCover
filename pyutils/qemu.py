#!/usr/bin/env python3
import os
import subprocess

import pyutils.config as config

from pyutils.server import RealClient, Server, group_clients_by_args, Client, MultiClient, LostClientException
from pyutils.result import Result, parse_results

# TODO: pass from diffuzz-server.py?
single_step = False
single_client = False
# Don't go high here, otherwise fuzzing gets slower.
# We want instant feedback and ping pong is fast.
# Too low broke qemu for some weird reason:
# deflate failed with code -5
max_batch_n = 5

class LocalQemu(Client):
    def __init__(self, seed):
        self.seed = seed
        self.current_respawn_iteration = 0

        self.client = None
        self._start_client()
        assert(self.get_client())

    # Pass attributes through
    def __getattr__(self, name):
        return getattr(self.get_client(), name)

    def schedule_packed_inputs(self, n_results: int, packed_inputs: bytes, priority: bool = False) -> int:
        respawn_ticket = self.current_respawn_iteration
        real_ticket = self.get_client().schedule_packed_inputs(n_results, packed_inputs, priority)
        return (respawn_ticket, real_ticket)

    def get_results(self, ticket: tuple[int, int]) -> list[Result]:
        respawn_ticket, real_ticket = ticket

        if self.current_respawn_iteration != respawn_ticket:
            # Then the client died already so don't try to lookup ticket
            e = LostClientException(f"Lost local qemu before", self, None, True)
            e.real_client = None
            e.remove_client = None
            raise e

        client = self.get_client()
        try:
            res = client.get_results(real_ticket)
            for r in res:
                r.client = self
            return res
        except LostClientException as e:
            e.client = self
            if e.remove_client:
                e.remove_client = None
                # Always just restart
                self.client = None
                self.current_respawn_iteration+=1
            raise e

    def to_dict(self):
        return self.get_client().to_dict()

    def __str__(self):
        assert(self.client)
        return f"LocalQemu({str(self.get_client())})"

    def __repr__(self):
        return self.__str__()

    def get_client(self):
        if not self.client:
            self._start_client()
        return self.client

    def _start_client(self):
        server = Server("127.0.0.1", None, 1, max_batch_n, single_step=single_step, single_client=single_client, seed=self.seed, interactive=False)

        server.start()
        port = server.get_port()

        os.system(f"ARCH={config.ARCH} {config.FRAMEWORK_ROOT}/bin/qemu-wrapped \"qemu-v9\" \"{config.FRAMEWORK_ROOT}/diffuzz-client 127.0.0.1 {port} qemu-v9\"&")

        server.join()

        self.client = MultiClient(server.clients)

# TODO: build client, make sure to synchronize or something

if __name__ == '__main__':
    import argparse

    from pyutils.shared_logic import parse_and_set_flags
    parse_and_set_flags()

    from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_common_argparser, parse_flags

    from pyutils.generation.generators.offlinerandomdifffuzzgenerator import OfflineRandomDiffFuzzGenerator

    parser = get_common_argparser()
    parser_add_common_extension_parsing(parser)
    OfflineRandomDiffFuzzGenerator.parser_add_args(parser)

    args = parser.parse_args()
    generator = OfflineRandomDiffFuzzGenerator.from_args(args)
    collection = generator.instruction_collection

    flags, non_repro_flags = parse_flags(args)
    generator_flags, generator_non_repro_flags = generator.get_build_flags()
    flags |= generator_flags
    non_repro_flags |= generator_non_repro_flags

    generator.early_init(flags.union(non_repro_flags))
    generator.late_init(flags.union(non_repro_flags))

    subprocess.run(["build-client", "--target", "diffuzz-client", "--out", f"{config.FRAMEWORK_ROOT}/diffuzz-client", "--build-flags", " ".join(flags.union(non_repro_flags))])

    qemu = LocalQemu()
    assert(qemu.client)
    # for client in qemu.client.clients:
    #     import socket
    #     client.socket.shutdown(socket.SHUT_RDWR)
    #     client.socket.close()
    # qemu2 = LocalQemu()
    # qemu.schedule_packed_inputs(1, b"A")

    inp = next(generator.generate(0, 1))
    print(inp.to_input_with_values().to_yaml(instruction_collection=collection))

    print(inp.pack())

    os.system("pkill qemu")
    import time
    time.sleep(0.5)

    # Test reconnection
    for i in range(4):
        print(f"iteration {i}", "Remaining clients", len(qemu.client.clients))
        ticket = qemu.schedule_packed_inputs(1, inp.pack())
        try:
            results = qemu.get_results(ticket)
            assert(False)
        except LostClientException as e:
            print("exception as expected on iteration", i)
            pass
    ticket2 = qemu.schedule_packed_inputs(1, inp.pack())
    results = qemu.get_results(ticket2)
    print(results[0])

    os._exit(0)
