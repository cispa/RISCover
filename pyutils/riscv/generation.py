#!/usr/bin/env python3
import random
import struct
import numpy as np
import os
import time

import sys
sys.path.append(os.path.dirname(__file__))

from pyutils.instruction_collection import InstructionCollection

import pyutils.config as config

from pyutils.inp import InputWithRegSelect
from pyutils.util import VEC_REG_SIZE, gp, fp, vec
from fuzzing_value_map import fuzzing_value_map_gp, fuzzing_value_map_fp

reg_mapping = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]

                            # this means all regs are broken
# TODO: move to eval/some filter.py file
def filter_broken_thead(instr, broken_regs=[], time_to_bug=False):
    global rvop_thead_filter, rvop_vector_filter
    if not rvop_thead_filter:
        rvop_thead_filter = RvOpcodesDis(["custom/rv_thead"])
        rvop_vector_filter = RvOpcodesDis(["rv_v"])
    mne_vec = dis_riscv_opcodes(rvop_vector_filter, instr)
    if mne_vec in ["vse128.v", "vsse128.v", "vse256.v", "vsse256.v", "vse512.v", "vsse512.v", "vse1024.v", "vsse1024.v"]:
        if time_to_bug:
            print("[Time to bug] Found one of the buggy vector instructions:", mne_vec, time.time())
            exit(0)
        return True
    mne = dis_riscv_opcodes(rvop_thead_filter, instr)
    if mne in ["th.lbib", "th.ldia", "th.lbia", "th.lwuia",
               # not tested
                "th.lbuia", "th.lbuib", "th.ldib", "th.lhuib", "th.lwuib", "th.lwib", "th.lhuia", "th.lhia", "th.lwia", "th.lhib"]:

        rd = rvop_thead_filter.extract_reg(instr, "rd")
        if broken_regs == [] or rd in broken_regs:
            rs1 = rvop_thead_filter.extract_reg(instr, "rs1")
            if rs1 == rd:
                # sys.stdout.write("\033[K")
                # print("skipped", mne, rd, rs1)
                if time_to_bug:
                    print("[Time to bug] Found the C906 locking sequence:", mne, rs1, rd, time.time())
                    exit(0)
                return True

    return False

# def get_random_known_instr():
#     # TODO: ratio?
#     # only do compressed 1/8 of times
#     compressed=random.randrange(0, 8) == 0
#     while True:
#         if compressed:
#             instr = (0x0001 << 16) | random.randint(0, 0xffff)
#         else:
#             instr = random.randint(0, 0xffffffff) | 0x3

#         d = md.disasm(instr.to_bytes(4, byteorder='little'), 0)
#         decoded = False
#         for i in d:
#             # skip instructions that write to msrs since they might change CPU behavior
#             # like MSR 2048 on T-HEAD
#             if "csr" in str(i):
#                 decoded = False
#                 break
#             decoded = True
#             # if "l" in i.mnemonic or "s" in i.mnemonic:
#             #     # print(i.mnemonic)
#             #     continue
#                 # exit(0)
#             # p("0x%s\t%s\t%s" % (i.bytes.hex(), i.mnemonic, i.op_str))
#             break
#         if decoded:
#             return instr

# TODO: check which known extensions are supported
# then check unknown

def gen_unknown_instr_sequentially_until(rvop, start_at, until, ifilter=None) -> list[int]:
    l = []
    while start_at < until:
        # TODO: add nop for compressed
        d = dis_riscv_opcodes(rvop, start_at)
        if not d and (not ifilter or not ifilter(start_at)):
            l += [start_at]
        start_at += 1
    return l

def gen_known_instrs(rvop):
    l = []
    dis = []
    for i in rvop.insns:
        insn = rvop.insns[i]
        matc = insn["match"]
        b = matc
        if b & 0x3 != 0x3:
            b |= 0x0001 << 16
        l += [b]
        dis += [i]

    return l, dis

def exec_per_sec(call, n=10):
    import timeit
    time = timeit.timeit(call, number=n)
    return n/time

def main():
    m=10000

    mm=100000
    matches=0
    nonmatches=0
    rvop = RvOpcodesDis()
    rvop.pretty_print_instr(0x34834813)
    exit(0)

    col = ArmInstructionCollection()
    print(col, flush=True)
    print(len(gen_new_inputs(col, m, 26*[0], 26*[0], 1, 32)))
    print(exec_per_sec(lambda: hash(str(gen_new_inputs(col, m, 26*[0], 26*[0], 1, 32))), n=30)*m, "e/s")
    print(len([gen_new_input(col, 26*[0], 26*[0], 1, 32, i) for i in range(m)]))
    print(exec_per_sec(lambda: hash(str([gen_new_input(col, 26*[0], 26*[0], 1, 32, i) for i in range(m)])), n=30)*m, "e/s")
    print(len([gen_new_input2(col, 26*[0], 26*[0], 1, 32, i) for i in range(m)]))
    print(exec_per_sec(lambda: hash(str([gen_new_input2(col, 26*[0], 26*[0], 1, 32, i) for i in range(m)])), n=30)*m, "e/s")

    inputs = [gen_new_input(col, 26*[0], 26*[0], 1, 32, (10000000+i)^3000000) for i in range(2500)]
    print("exact same line", len(inputs))
    return

    # for i in gen_new_instr_sequentially(mm, 0x00000013):
    for i in np.random.randint(0, 0xffffffff, size=mm):
        d = dis_riscv_opcodes(rvop, i)
        if d:
            matches+=1
            print(hex(i), d)
        else:
            nonmatches+=1
    print("matches", matches/mm)
    print("nonmatches", nonmatches/mm)

    print(dis_riscv_opcodes(rvop, 0x0180000b))

    m=100000
    print(exec_per_sec(lambda: [dis_riscv_opcodes(rvop, i) for i in gen_new_instr_sequentially(m, 0)])*m, "e/s")

    print("unknown:", exec_per_sec(lambda: gen_new_unknown_instr_sequentially(m, 0))*m, "e/s")

    print("some unknown", [hex(i) for i in gen_new_unknown_instr_sequentially(10, 0x348284)])

    for i in gen_known_instrs():
        d = dis_riscv_opcodes(i)
        print(hex(i), d, insns[d]["extension"])
    print(len(gen_known_instrs()))
    print(pack_instrs(pad_instrs(gen_known_instrs(), 300)))

    exit(0)

    # for _ in range(0, 20):
    #     # print(hex(get_random_known_instr(verbose=True, mnemonic="addi")))
    #     print(hex(get_random_known_instr(verbose=True)))
    #     print()
    gen_new_batches(10000, 26*[0], 26*[0])
    gen_new_batches(10000, 26*[0], 26*[0])
    gen_new_batches(10000, 26*[0], 26*[0])

    print(exec_per_sec(lambda: gen_new_batches(m, 26*[0], 26*[0]), n=30)*m, "e/s")
    # print(exec_per_sec(lambda: gen_new_batches2(m, 26*[0], 26*[0]), n=30)*m, "e/s")

    print(exec_per_sec(lambda: pack_inputs(gen_new_batches(m, 26*[0], 26*[0])), n=30)*m, "e/s")
    # print(exec_per_sec(lambda: pack_batches(gen_new_batches2(m, 26*[0], 26*[0])), n=30)*m, "e/s")

if __name__ == '__main__':
    main()
