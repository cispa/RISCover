#!/usr/bin/env python3
import sys
import argparse
from generation import dis_riscv_opcodes, RvOpcodesDis

# instr = int(sys.argv[1], 16)

parser = argparse.ArgumentParser()
parser.add_argument('--extensions', nargs='+')
# parser.add_argument('--thead', action='store_true')
parser.add_argument('instr', type=lambda x: int(x, 16))
args = parser.parse_args()

if args.extensions:
    rvop = RvOpcodesDis(args.extensions)
    print(dis_riscv_opcodes(rvop, args.instr))
else:
    rvop = RvOpcodesDis()
    print("normal:", dis_riscv_opcodes(rvop, args.instr))
    rvop = RvOpcodesDis(extensions=["custom/rv_v_0.7.1"])
    print("0.7.1:", dis_riscv_opcodes(rvop, args.instr))
    rvop = RvOpcodesDis(extensions=["custom/rv_thead"])
    print("thead:", dis_riscv_opcodes(rvop, args.instr))
