#!/usr/bin/env python3
import re
import os
import sys
import json5

from thead_parser import parse_all, Instruction, Mask

parsed = parse_all()

# fld=Instruction()
# fld.mnemonic = "fld _rd_ imm(_rs1_)"
# fld.opcode = Mask(7, 0, 0b0000111)
# fld.mask = Mask(3, 12, 0b011)
# fld.others={
#     "rd": Mask(5, 7),
#     "rs1": Mask(5, 15),
#     "imm[11:0]": Mask(12, 20)
# }
# parsed+=[fld]

parsed+=[
    Instruction.init(Mask(7, 0, 0b0000111),     Mask(3, 12, 0b001),     "flh _rd_ imm(_rs1_)",      { "rd": Mask(5, 7), "rs1": Mask(5, 15), "imm[11:0]": Mask(12, 20) }),
    Instruction.init(Mask(7, 0, 0b0100111),     Mask(3, 12, 0b001),     "fsh _rs2_ imm(_rs1_)",     { "imm[4:0]": Mask(5, 7), "rs1": Mask(5, 15), "rs2": Mask(5, 20), "imm[11:5]": Mask(7, 25) }),
    Instruction.init(Mask(7, 0, 0b1000011),      Mask(2, 25, 0b10),     "fmadd.h",                  {}),
    Instruction.init(Mask(7, 0, 0b1000111),      Mask(2, 25, 0b10),     "fmsub.h",                  {}),
    Instruction.init(Mask(7, 0, 0b1001011),      Mask(2, 25, 0b10),    "fnmsub.h",                  {}),
    Instruction.init(Mask(7, 0, 0b1001111),      Mask(2, 25, 0b10),    "fnmadd.h",                  {}),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00000),     "fadd.h", {}),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00001),     "fsub.h", {}),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00010),     "fmul.h", {}),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00011),     "fdiv.h", {}),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b01011),     "fsqrt.h", {}, [Mask(20, 5, 0)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00100),     "fsgnj.h", {}, [Mask(12, 3, 0b000)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00100),    "fsgnjn.h", {}, [Mask(12, 3, 0b001)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00100),    "fsgnjx.h", {}, [Mask(12, 3, 0b010)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00101),      "fmin.h", {}, [Mask(12, 3, 0b000)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b00101),      "fmax.h", {}, [Mask(12, 3, 0b001)]),

    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b10100),       "feq.h", {}, [Mask(12, 3, 0b010)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b10100),       "flt.h", {}, [Mask(12, 3, 0b001)]),
    Instruction.init(Mask(7, 0, 0b1010011),      Mask(2, 25, 0b10).add(5, 27, 0b10100),       "fle.h", {}, [Mask(12, 3, 0b000)]),
]

by_opcode = {}
for insn in parsed:
    assert(insn.opcode.mask == 0b1111111)
    val=insn.opcode.val
    if val not in by_opcode:
        by_opcode[val]=[insn]
    else:
        by_opcode[val]+=[insn]

def disasm_one(insn):
    op = insn & 0b1111111
    if op in by_opcode:
        # if i.opcode.matches(insn):
        for i in by_opcode[op]:
            if i.mask.matches(insn):
                for c in i.further_constraints:
                    if not c.matches(insn):
                        continue
                j=i
                j.val = insn
                return j
    return None

def disasm(insn):
    ret = []
    res = disasm_one(insn)
    if res:
        ret+=[res]
    if insn & 0x3 == 0x3:
        res = disasm_one(insn>>16)
        if res:
            ret+=[res]
    return ret

def dis_and_assert(instr, should_be):
    d=disasm(instr)[0]
    print(d)
    assert(should_be in d.mnemonic)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        # dis_and_assert(0x13, "nop")
        dis_and_assert(0x0180000b, "th.sync")
        # dis_and_assert(0xa52480d3, "fle.h")
        # dis_and_assert(0x253914d3, "fsgnjn.h")
        # dis_and_assert(0xa52480d3, "fle.h")
        # dis_and_assert(0x253914d3, "fsgnjn.h")
        dis_and_assert(0x4431508b, "th.srw")
        dis_and_assert(0x8001108b, "th.tstnbz")
        dis_and_assert(0x0821208b, "th.ext")
        dis_and_assert(0x0431108b, "th.addsl")
        dis_and_assert(0x0100000b, "th.icache.iall")
        dis_and_assert(0x0180000b, "th.sync")
        # dis_and_assert(0x1e3120af, "sc.w.aqrl")
        # dis_and_assert(0x2621a0af, "amoxor.w.aqrl")
        dis_and_assert(0x4231108b, "th.mvnez")
        dis_and_assert(0x0231108b, "th.addsl")
        dis_and_assert(0x8811118b, "th.tst")
        dis_and_assert(0x6211608b, "th.flrd")
        # dis_and_assert(0xe40480d3, "fmv.x.hw")
        # dis_and_assert(0xf40104d3, "fmv.hw.x")
        dis_and_assert(0x0040000b, "ipush")
        dis_and_assert(0x2031108b, "th.mula")
        dis_and_assert(0xfc31408b, "th.ldd")

        # mine
        # dis_and_assert(0b100001011000100000111, "fld")

        dis_and_assert(0x109187, "flh")
        dis_and_assert(0b001000000100111, "fsh")
    else:
        insn = int(sys.argv[1], 16)
        for i in disasm(insn):
            print(i)

        # i=0
        # while True:
        #     disasm(0b100001011000100000111)
        #     if i % 100000 == 0:
        #         print(i)
        #     i+=1
