#!/usr/bin/env python3
import random

from pyutils.arm.arm_instruction_collection import ArmInstructionCollection

def exec_per_sec(call, n=10):
    import timeit
    time = timeit.timeit(call, number=n)
    return n/time

def gen_new_instr_sequentially(n, last):
    return list(range(last, last+n))

def main():
    collection = ArmInstructionCollection()

    # for tag, data in collection.instructions.items():
    #     print(f"Tag: {tag}")
    #     for field in data['fields']:
    #         print(f"  {field['range']} {field['name']} {field['mask']:b} {field['value']:b}")
    #     mask, value = ArmInstructionCollection.gen_combined_mask(data)
    #     print(bin(mask), bin(value))

    # # print(collection.mask_map)
    # print("len mask map", len(collection.mask_map))
    # # print("a")
    # for _, i in collection.mask_map.items():
    #     print(len(i), i)

    # for extension, instructions in collection.instr_per_extension.items():
    #     print()
    #     print(extension)
    #     print(instructions)

    # ./risugen --seed 4 --numinsns 10 aarch64.risu vqshlimm.out && aarch64-unknown-linux-gnu-objdump -m aarch64 -b binary -D vqshlimm.out
    print("thumb nop	", collection.disassemble(0xbf00))
    print("smlal	", collection.disassemble(0x0e798124))
    print("sha512su0	", collection.disassemble(0xcec081d0))
    print("bcax	", collection.disassemble(0xce20496e))

    print(0, collection.disassemble(0))

    print("?	", collection.disassemble(0x12087f65))

    print("add x0, x1, 1	", collection.disassemble(0x91000420))
    print("brk	", collection.disassemble(0xd4200000))
    print("ldr	", collection.disassemble(0xf9400020))

    print(len(collection.instructions.keys()))
    # print(collection.instructions.keys())

    print(hex(collection.init_instr(None, 32, mnemonic="A64.simd_dp.crypto4.BCAX_VVV16_crypto4", verbose=True)))
    print(hex(collection.init_instr(None, 32, mnemonic="A64.simd_dp.crypto4.BCAX_VVV16_crypto4", verbose=True)))
    print(collection.disassemble(collection.init_instr(None, 32, mnemonic="A64.simd_dp.crypto4.BCAX_VVV16_crypto4", verbose=True)))
    print(collection.disassemble(collection.init_instr(None, 32, mnemonic="A64.simd_dp.crypto4.BCAX_VVV16_crypto4", verbose=True)))

    # add x0, x1, 1
    collection.pretty_print_instr(0x91000420)
    # brk
    collection.pretty_print_instr(0xd4200000)

    print("add x5, x1, 42")
    collection.pretty_print_instr(collection.instr_set_encoding(collection.instr_set_encoding(0x91000420, "Rd", 5), "imm12", 42))

    # import json
    # a = []
    # for i in collection.disassemble(0xce20496e):
    #     # print(json.dumps(collection.instructions[i], indent=4))
    #     print(collection.instructions[i])
    #     b = collection.instructions[i]
    #     b["mnemonic"] = ""
    #     a += [b]
    # assert(a[0] == a[1])
    # assert(a[1] == a[2])

    mm=1000000
    matches=0
    nonmatches=0
    # for i in gen_new_instr_sequentially(mm, 0x00000013):
    for _ in range(mm):
        i = random.randint(0, 0xffffffff)
        d = collection.disassemble(i)
        if d:
            matches+=1
            # print(hex(i), d)
        else:
            nonmatches+=1
    print("matches", matches/mm)
    print("nonmatches", nonmatches/mm)

    m=200000
    print(exec_per_sec(lambda: sum(1 if collection.disassemble(random.randint(0, 0xffffffff)) else 0 for _ in range(m)))*m, "e/s")
    # print(exec_per_sec(lambda: [collection.disassemble(i) for i in gen_new_instr_sequentially(m, 0)])*m, "e/s")

    # print("unknown:", exec_per_sec(lambda: collection.gen_new_unknown_instr_sequentially(m, 0))*m, "e/s")

    # print("some unknown", [hex(i) for i in collection.gen_new_unknown_instr_sequentially(10, 0x348284)])

if __name__ == '__main__':
    main()
