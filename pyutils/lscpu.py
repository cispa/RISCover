#!/usr/bin/env python3
import re
import sys

class Microarchitecture:
    def __init__(self, model_name, num_cores, num_sockets, threads_per_core, flags):
        self.model_name = model_name
        self.num_cores = num_cores
        self.num_sockets = num_sockets
        self.threads_per_core = threads_per_core
        self.flags = flags

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        if self.model_name != other.model_name:
            return False
        if self.num_cores != other.num_cores:
            return False
        if self.num_sockets != other.num_sockets:
            return False
        if self.threads_per_core != other.threads_per_core:
            return False
        if self.flags != other.flags:
            return False
        return True

    def __repr__(self):
        return (f"Microarchitecture(midr='{self.midr}', model_name='{self.model_name}', num_cores={self.num_cores}, "
                f"num_sockets={self.num_sockets}, threads_per_core={self.threads_per_core}, flags={self.flags})")

    def get_num_logical_cores(self):
        return self.num_sockets * self.num_cores * self.threads_per_core

    def to_dict(self):
        return {
            'midr': self.midr,
            'model_name': self.model_name,
            'num_cores': self.num_cores,
            'num_sockets': self.num_sockets,
            'threads_per_core': self.threads_per_core,
            'flags': list(self.flags)
        }

    @classmethod
    def from_dict(cls, data):
        return cls(
            model_name=data.get('model_name'),
            num_cores=data.get('num_cores'),
            num_sockets=data.get('num_sockets'),
            threads_per_core=data.get('threads_per_core'),
            flags=set(data.get('flags'))
        )


class CPUInfo:
    def __init__(self, architecture: str, vendor: str, microarchitectures: list[Microarchitecture]):
        self.architecture = architecture
        self.vendor = vendor
        self.microarchitectures = microarchitectures

    def __repr__(self):
        return (f"CPUInfo(architecture='{self.architecture}', vendor='{self.vendor}', "
                f"microarchitectures={self.microarchitectures})")

    def get_microarchitecture_by_core_id(self, core: int):
        current = 0
        for micro in self.microarchitectures:
            if current + micro.get_num_logical_cores() > core:
                return micro
            current += micro.get_num_logical_cores()

        print(f"Tried to get microarchitecture for invalid core id. Queried {core} on {self}.", file=sys.stderr)
        exit(9)

    def to_dict(self):
        return {
            'architecture': self.architecture,
            'vendor': self.vendor,
            'microarchitectures': [micro.to_dict() for micro in self.microarchitectures]
        }

    @classmethod
    def from_dict(cls, data):
        return cls(
            architecture=data.get('architecture'),
            vendor=data.get('vendor'),
            microarchitectures=[Microarchitecture.from_dict(micro) for micro in data.get('microarchitectures', [])]
        )

def parse_lscpu_output(content: str) -> CPUInfo:
    # Initialize result
    architecture = None
    vendor = None
    cpus = []

    # Regex patterns for extraction
    architecture_pattern = re.compile(r'Architecture:\s+(\S+)', re.MULTILINE)
    vendor_pattern = re.compile(r'Vendor ID:\s+(\S+)', re.MULTILINE)
    model_name_pattern = re.compile(r'Model name:\s+(.+?)$', re.MULTILINE)
    if "cluster" in content:
        socket_small="cluster"
        socket_big="Cluster"
    else:
        socket_small="socket"
        socket_big="Socket"
    core_pattern = re.compile(rf'Core\(s\) per {socket_small}:\s+(\d+)', re.MULTILINE)
    socket_pattern = re.compile(rf'{socket_big}\(s\):\s+(\d+)', re.MULTILINE)
    thread_pattern = re.compile(r'Thread\(s\) per core:\s+(\d+)', re.MULTILINE)
    flags_pattern = re.compile(r'Flags:\s+([\w\s\.\-]+?)$', re.MULTILINE)

    # Extract architecture
    architecture_match = architecture_pattern.search(content)
    if architecture_match:
        architecture = architecture_match.group(1)

    # Extract vendor
    vendor_match = vendor_pattern.search(content)
    if vendor_match:
        vendor = vendor_match.group(1)
        if vendor.startswith("0x"):
            # lab70, vendor id is 0x710 there
            vendor = None

    # Extract CPU information
    for query in ["Model name:", "Model:"]:
        if query not in content:
            continue
        cpu_blocks = content.split(query)[1:]
        if len(cpu_blocks) > 1:
            # multiple microarchitectures
            cpu_blocks = list(map(lambda c: query+" "+c, cpu_blocks))
        else:
            cpu_blocks = [content]
        break
    # If no model name is found just hardcode the info
    assert(cpu_blocks)
    for block in cpu_blocks:
        model_name_match = model_name_pattern.search(block)
        core_match = core_pattern.search(block)
        socket_match = socket_pattern.search(block)
        thread_match = thread_pattern.search(block)
        flags_match = flags_pattern.search(block)

        if model_name_match:
            model_name = model_name_match.group(1)
            # TODO: model_name can be "-" (lab71 with static lscpu)
        else:
            model_name = "unknown"

        if flags_match:
            flags = set(flags_match.group(1).strip().split(" "))
        else:
            flags = set()

        cpu_info = Microarchitecture(
            model_name=model_name,
            num_cores=int(core_match.group(1)),
            num_sockets=int(socket_match.group(1)),
            threads_per_core=int(thread_match.group(1)),
            flags=flags
        )
        cpus.append(cpu_info)

    return CPUInfo(architecture=architecture, vendor=vendor, microarchitectures=cpus)

if __name__ == "__main__":
    import sys
    with open(sys.argv[1], 'r') as file:
        content = file.read()
    parsed_data = parse_lscpu_output(content)
    print(parsed_data)
    print(parsed_data.get_microarchitecture_by_core_id(0))
    # print(parsed_data.get_microarchitecture_by_core_id(6))
