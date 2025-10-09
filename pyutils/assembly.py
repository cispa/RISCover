#!/usr/bin/env python3
from keystone import *
from pwn import asm, context

# Hide warnings such as:
# [!] Could not find system include headers for riscv64-linux
context.log_level = 'error'

import pyutils.config as config

match config.ARCH:
    case "riscv64":
        # keystone not there for riscv64 yet
        ks = None
        pass
    case "aarch64":
        ks = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)

def asm_keystone(instr):
    encoding, _ = ks.asm(instr)
    return [
        int.from_bytes(encoding[i:i+4], byteorder='little')
        for i in range(0, len(encoding), 4)
    ]

def asm_opcodes(instr):
    code = asm(instr, arch=config.ARCH)
    return [
        int.from_bytes(code[i:i+4], byteorder='little')
        for i in range(0, len(code), 4)
    ]
