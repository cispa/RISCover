#!/usr/bin/env python3
import capstone
from pwn import disasm

import pyutils.config as config

match config.ARCH:
    case "riscv64":
        md = capstone.Cs(capstone.CS_ARCH_RISCV, capstone.CS_MODE_RISCV64|capstone.CS_MODE_RISCVC)
    case "aarch64":
        # TODO: thumb
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        # md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)

def disasm_capstone(instr_seq):
    d = md.disasm(b"".join([instr.to_bytes(4, byteorder='little') for instr in instr_seq]), 0)
    lines=[]
    for j in d:
        lines+=["%s %s" % (j.mnemonic, j.op_str)]
    return lines

def disasm_opcodes(instr_seq):
    d = []
    for instr in instr_seq:
        d += [disasm(instr.to_bytes(4, byteorder='little'), offset=False, byte=False, arch=config.ARCH).replace(" ; undefined", "")]
    return d
