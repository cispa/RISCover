#!/usr/bin/env python3
import sys
import capstone

instr = int(sys.argv[1], 16).to_bytes(4, byteorder='little')
# nop
instr+=b"\x01\x00"

md = capstone.Cs(capstone.CS_ARCH_RISCV, capstone.CS_MODE_RISCV64|capstone.CS_MODE_RISCVC)

# Disassemble and print the instructions
for i in md.disasm(instr, 0x1000):
    print("0x%s\t%s\t%s" % (i.bytes.hex(), i.mnemonic, i.op_str))
