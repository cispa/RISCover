#!/usr/bin/env python3

import os
import sys
sys.path.append(os.path.dirname(__file__))

from generation import dis_riscv_opcodes, RvOpcodesDis

pairs = [
    ("specified", RvOpcodesDis(["rv*"])),
    ("specified + unratified", RvOpcodesDis(["rv* unratified/rv*"])),
    ("vector", RvOpcodesDis(["rv_v"])),
    ("vector 0.7.1", RvOpcodesDis(["custom/rv_v_0.7.1"])),
    ("thead", RvOpcodesDis(["custom/rv_thead"])),
]

stats = {}
for (i, _) in pairs:
    stats[i] = 0
stats["known"] = 0
stats["unknown"] = 0

m = 0x100000000
# m = 0x100000

matches = 0
nonmatches = 0
for i in range(0, m):
    if (i % (m // 100)) == 0:
        print(int(i*100/m))
    known = False
    for (k, rvop) in pairs:
        d = dis_riscv_opcodes(rvop, i)
        if d:
            known = True
            stats[k] += 1
    if known:
        stats["known"] += 1
    else:
        stats["unknown"] += 1

print("ISA                        Percentage")
for (i, v) in stats.items():
    # print(i, f"{round(v*100/m, 2)}%")
    print(f"{i:<30} {v*100/m:>10.2f}%")
