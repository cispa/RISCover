#!/usr/bin/env python3
import yaml
from itertools import combinations

import pyutils.config as config

from pyutils.result import Result
from pyutils.server_util import real_clients_from_client_tuples
from pyutils.server import ClientMeta
from pyutils.server_util import result_to_clients_to_str
from pyutils.inp import InputWithSparseValues
from pyutils.util import hex_yaml
from pyutils.lscpu import Microarchitecture, CPUInfo

# TODO: migrate to input?
class Mapping():
    def __init__(self, start, n, prot, val):
        self.start = start
        self.n = n
        self.prot = prot
        self.val = val

    def to_dict(self):
        d = {}
        d["start"] = self.start
        d["n"] = self.n
        d["prot"] = self.prot
        d["val"] = self.val
        return d

    @classmethod
    def from_dict(cls, d):
        start = d["start"]
        n = d["n"]
        prot = d["prot"]
        val = d["val"]
        # NOTE: uncomment here for setting whole memory to 0 or something else
        # val = n*"00"
        # val = (n//4)*"d503201f"
        return cls(start, n, prot, val)

class Repro():
    def __init__(self, inp, flags, core=None, result_to_clients=None, generator=None, counter=None, mappings=None, full_dict=None):
        self.inp = inp
        self.flags = flags
        self.core = core
        if result_to_clients:
            if type(list(result_to_clients.values())[0][0]) == tuple:
                # We only want real clients here so extract them
                self.result_to_clients = {}
                for result, client_tuples in result_to_clients.items():
                    self.result_to_clients[result] = real_clients_from_client_tuples(client_tuples)
            else:
                self.result_to_clients = result_to_clients
        else:
            self.result_to_clients = None
        self.generator = generator
        self.counter = counter
        if mappings == None:
            mappings = []
        self.mappings = mappings
        self.full_dict = full_dict or {}

    # TODO: maybe input should have the disassembly in the first place, not sure though
    def to_dict(self, instruction_collection):
        d = self.full_dict
        d["input"] = self.inp.to_dict(instruction_collection)
        if self.result_to_clients:
            d["results"] = []
            for result, clients in self.result_to_clients.items():
                d["results"] += [{
                    "result": result.to_dict(),
                    # Filling cpu info into every repo results in a lot of data
                    "clients": list(map(lambda c: c.to_dict(log_cpu=False), clients))
                }]

        d["mappings"] = [mapping.to_dict() for mapping in self.mappings]

        if self.generator != None:
            d["generator"] = self.generator.to_dict()
        if self.counter != None:
            d["counter"] = self.counter
        d["arch"] = config.ARCH
        d["flags"] = list(self.flags)
        if self.core != None:
            d["core"] = self.core

        return d

    def to_yaml(self, instruction_collection):
        return hex_yaml(self.to_dict(instruction_collection))

    def results_to_str(self, color=True):
        return "\n".join(f"{r} differs" for r in self.all_diffs())+"\n\n"+result_to_clients_to_str(self.result_to_clients, color=color)

    def to_yaml_with_comments(self, instruction_collection):
        assert(self.result_to_clients)
                                     # strip for empty lines
        if "cpu" in self.full_dict:
            return "\n".join([("# " + x).strip() for x in ([self.full_dict["cpu"]]+[""]+self.results_to_str(color=False).split("\n"))])+"\n\n"+self.to_yaml(instruction_collection)
        else:
            return "\n".join([("# " + x).strip() for x in self.results_to_str(color=False).split("\n")])+"\n\n"+self.to_yaml(instruction_collection)

    @classmethod
    def from_dict(cls, data, inp_cls=InputWithSparseValues):
        assert(data["arch"] == config.ARCH)

        inp = inp_cls.from_dict(data["input"])

        flags = set(data["flags"])
        if "core" in data:
            core = data["core"]
        else:
            core = None

        mappings = []
        if "mappings" in data:
            for m in data["mappings"]:
                mappings+=[Mapping.from_dict(m)]

        if "results" in data:
            result_to_clients = {}
            for res_data in data["results"]:
                result = Result.from_dict(res_data["result"])
                if "clients" in res_data:
                    clients = [ClientMeta.from_dict(client) for client in res_data["clients"]]
                    result_to_clients[result] = clients
                else:
                    result_to_clients = None
        else:
            result_to_clients = None

        # TODO
        # if "generator" in data:
        #     generator = Generator.from_dict(data["generator"])
        # else:
        #     seed = None

        return cls(inp, flags, core=core, result_to_clients=result_to_clients, mappings=mappings, full_dict=data)

    def similar(self, other) -> bool:
        if len(self.result_to_clients) != len(other.result_to_clients):
            return False

        if self.all_diffs(no_regs=True) != other.all_diffs(no_regs=True):
            return False

        # TODO: this is not perfect because it assumes same order but for now fine
        # TODO(now): is this fine? because we change order by voting...
        for (res1, clients1), (res2, clients2) in zip(self.result_to_clients.items(), other.result_to_clients.items()):
            if not res1.similar(res2):
                return False
            if len(clients1) != len(clients2):
                return False
            for client1, client2 in zip(clients1, clients2):
                if not client1.similar(client2):
                    return False
        return True

    # Collect all the diffs between all of the results
    def all_diffs(self, no_regs=False) -> set[str]:
        diffs = set()
        for res1, res2 in combinations(map(lambda x: x[0], self.result_to_clients.items()), 2):
            diffs.update(res1.diff(res2, no_regs))
        return diffs

    @classmethod
    def from_file(cls, path, inp_cls=InputWithSparseValues):
        with open(path, 'r') as f:
            return cls.from_dict(yaml.safe_load(f), inp_cls=inp_cls)
