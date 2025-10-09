#!/usr/bin/env python3
import yaml
import sys

from pyutils.repro import Repro
from pyutils.shared_logic import parse_and_set_flags, parse_flags
parse_and_set_flags()

import pyutils.config as config
from pyutils.inp import InputWithValuesRiscv64
from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_most_common_argparser
from pyutils.util import bcolors, color_str, gp, fp
from pyutils.riscv.riscv_instruction_collection import RiscvInstructionCollection

parser = get_most_common_argparser()
parser.add_argument('logfile')

parser.add_argument('--vector', action='store_true')
parser.add_argument('--floats', action='store_true')

parser_add_common_extension_parsing(parser)
args = parser.parse_args()
collection = get_collection_from_args(args)

assert(config.FLOATS)

with open(args.logfile, "r") as f:
    values = [int(x, 16) for x in filter(lambda x: "0x" in x, f.readlines())]

assert(len(values) == len(gp)+len(fp))

_gp = values[0:len(gp)]
_fp = [x & 0xffffffffffffffff for x in values[len(gp):]]

instr_seq = [0x13]
inp = InputWithValuesRiscv64(instr_seq=instr_seq, _gp=_gp, _fp=_fp)
collection = get_collection_from_args(args)
print(inp.to_dict(collection))

flags, _ = parse_flags(args)
repro = Repro(inp=inp, flags=flags)

out = repro.to_yaml(collection)
print()
print(out)
path = f"spike_repro.yaml"
with open(path, "w") as f:
    f.write(out)
print(f"Wrote repro to file {path}")

print(color_str("Manually set the instr sequence", bcolors.WARNING))

# spike --pc=0xd0000000 -d --isa=rv64imafdcv --debug-cmd=dbgfile myelf 2> out
# python spike_to_repro.py out
#
# dbgfile:
# until pc 0 0xd00753b8
# reg 0 ra
# reg 0 sp
# reg 0 gp
# reg 0 tp
# reg 0 t0
# reg 0 t1
# reg 0 t2
# reg 0 s0
# reg 0 s1
# reg 0 a0
# reg 0 a1
# reg 0 a2
# reg 0 a3
# reg 0 a4
# reg 0 a5
# reg 0 a6
# reg 0 a7
# reg 0 s2
# reg 0 s3
# reg 0 s4
# reg 0 s5
# reg 0 s6
# reg 0 s7
# reg 0 s8
# reg 0 s9
# reg 0 s10
# reg 0 s11
# reg 0 t3
# reg 0 t4
# reg 0 t5
# reg 0 t6
# freg 0 ft0
# freg 0 ft1
# freg 0 ft2
# freg 0 ft3
# freg 0 ft4
# freg 0 ft5
# freg 0 ft6
# freg 0 ft7
# freg 0 fs0
# freg 0 fs1
# freg 0 fa0
# freg 0 fa1
# freg 0 fa2
# freg 0 fa3
# freg 0 fa4
# freg 0 fa5
# freg 0 fa6
# freg 0 fa7
# freg 0 fs2
# freg 0 fs3
# freg 0 fs4
# freg 0 fs5
# freg 0 fs6
# freg 0 fs7
# freg 0 fs8
# freg 0 fs9
# freg 0 fs10
# freg 0 fs11
# freg 0 ft8
# freg 0 ft9
# freg 0 ft10
# freg 0 ft11
# quit
