#!/usr/bin/env python3
import sys, os
sys.path.append(os.getenv("FRAMEWORK_ROOT"))

from math import log2

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

import pyutils.config as config

from pyutils.disassembly import disasm_capstone, disasm_opcodes
from pyutils.arm.arm_instruction_collection import ArmInstructionCollection

from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed, parser_add_common_extension_parsing, get_collection_from_args

parser = get_common_argparser()

parser_add_common_extension_parsing(parser)

args = parse_args(parser)
assert(args.all_extensions)

collection = get_collection_from_args(args)

if config.ARCH == "aarch64":
    assert(config.VECTOR)
elif config.ARCH == "riscv64":
    assert(config.VECTOR and config.FLOATS)

end=1<<32
# end=1<<28

currently_skip=True
last_split=0
if config.ARCH == "aarch64":
    # NOTE: this assumes that the first range is a skip
    assert(not collection.disassemble(0))

if config.ARCH == "aarch64":
    byte_size = 4
elif config.ARCH == "riscv64":
    byte_size = 1

max_skip = 1
ranges_sum = 0
def log_skip(skip):
    global ranges_sum, max_skip
    fo.write(skip.to_bytes(byte_size, "little"))
    ranges_sum += skip
    max_skip = max(max_skip, skip)
    assert(skip < 1<<(8*byte_size))

with open("ranges", "wb") as fo:
    progress=0
    # progress_report=1<<27
    progress_report=1<<25
    for i in range(end):
        if i // progress_report != progress:
            progress = i // progress_report
            print(hex(i), i/(1<<32)*100)
            print(max_skip, log2(max_skip), ranges_sum)
        dis = collection.disassemble(i)
        if config.ARCH == "riscv64" and i == 0:
            dis = None
        if not dis:
            if currently_skip:
                pass
            else:
                currently_skip = True
                skip = i-last_split
                last_split = i

                log_skip(skip)
        else:
            if not currently_skip:
                pass
            else:
                currently_skip = False
                skip = i-last_split
                last_split = i

                log_skip(skip)

    if currently_skip:
        skip = end-last_split

        log_skip(skip)

print(max_skip, log2(max_skip), ranges_sum, end)
assert(ranges_sum == end)

# NOTE: c implementation relies on this
assert(currently_skip)
