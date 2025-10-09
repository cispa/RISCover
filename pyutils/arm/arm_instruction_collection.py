#!/usr/bin/env python3
import os
import random
import json
from json import JSONDecodeError

from pyutils.instruction_collection import InstructionCollection
from pyutils.immediate import SignedImmediate, UnsignedImmediate
from pyutils.generation.instruction import Instruction

def format_immediate_field_item(name, cls, ranges_list):
    ranges_map = {}
    for ranges in ranges_list:
        ranges_map[ranges] = cls(list(ranges))
    return (name, ranges_map)

immediates = dict([
    format_immediate_field_item( "imm2", UnsignedImmediate, [(( 9, 8),), ((13,12),), ((23,22),), ((11,10),)]),
    format_immediate_field_item( "imm3", UnsignedImmediate, [(( 7, 5),), ((12,10),), ((18,16),)]),
    format_immediate_field_item( "imm4", UnsignedImmediate, [((14,11),), ((19,16),)]),
    format_immediate_field_item( "imm5", UnsignedImmediate, [(( 9, 5),), ((20,16),)]),
    format_immediate_field_item( "imm6", UnsignedImmediate, [((10, 5),), ((15,10),), ((20,15),), ((21,16),)]),
    format_immediate_field_item( "imm7", UnsignedImmediate, [((20,14),), ((21,15),)]),
    format_immediate_field_item( "imm8", UnsignedImmediate, [(( 7, 0),), ((12, 5),), ((17,10),), ((20,13),)]),
    format_immediate_field_item( "imm9", UnsignedImmediate, [((20,12),)]),
    format_immediate_field_item("imm12", UnsignedImmediate, [((21,10),)]),
    format_immediate_field_item("imm13", UnsignedImmediate, [((17, 5),)]),
    format_immediate_field_item("imm14", UnsignedImmediate, [((18, 5),)]),
    format_immediate_field_item("imm16", UnsignedImmediate, [((20, 5),)]),
    format_immediate_field_item("imm19", UnsignedImmediate, [((23, 5),)]),
    format_immediate_field_item("imm26", UnsignedImmediate, [((25, 0),)]),

    # TODO: uimm vs imm
    format_immediate_field_item("uimm4", UnsignedImmediate, [((13,10),)]),
    format_immediate_field_item("uimm6", UnsignedImmediate, [((21,16),)]),

    format_immediate_field_item("simm7",   SignedImmediate, [((21,15),)]),
])

class ArmInstructionCollection(InstructionCollection):
    def __init__(self, extensions):
        self.extensions = extensions
        self.removed_instructions = set()

        try:
            db = os.path.join(os.path.dirname(__file__), 'instructions.json')
            with open(db, "r") as f:
                self.instructions = json.loads(f.read())
        except JSONDecodeError:
            print(f"Error: {db} is no valid json. You most likely did not pull git LFS files correctly yet. Try")
            exit(1)
        except FileNotFoundError:
            print("Error: 'instructions.json' file not found. Generate it with mra_tools_to_instruction_collection.py")
            exit(1)

        self.instructions = {
            instr: details
            for instr, details in self.instructions.items()
            if details.get("extension") in extensions
        }

        if len(self.instructions) == 0:
            raise Exception(f"No instructions generated from the specified extensions. Make sure they exist or match the glob you specified.\nYou used {', '.join(self.extensions)}\nThe command is {cmd}")

        self.weights = None

        self.update_maps()

    def to_dict(self):
        d = super().to_dict()
        d["type"] = "ArmInstructionCollection"
        return d

    def randomly_init_instr(self, mnemonic, num_regs):
        instr = self.init_instr(mnemonic=mnemonic)

        insn = self.instructions[mnemonic]

        b = instr.value

        # TODO: maybe store a list of variable fields
        for var in insn["fields"]:

            msb, lsb = var["range"]
            mask = var["mask"]
            name = var["name"]

            bits = msb-lsb+1
            inverted_mask = (~mask & ((1<<bits)-1)) << lsb

            # In 99% of cases, keep the immediate low
            # if any(map(lambda x: insn["mnemonic"].startswith(x), ("A64.control.branch_imm.", "A64.control.testbranch."))):
            #     if "imm" in (name or "") and random.randint(1, 100) <= 99:
            #         assert(name in ("imm26", "imm14"))
            #         if name == "imm26":
            #             bits = 26
            #         else:
            #             bits = 14
            #         imm = random.randint(-5, 5)
            #         if imm < 0:
            #             imm = (1<<bits) + imm
            #         b |= imm << lsb
            #         continue

            set_val = 0

            if name in set(("Rd", "Rn", "Rm")):
                # print(bits)
                #
                # assert(bits == 5 or bits == 3)
                assert(bits == 5 or bits == 4)
                # if insn["mnemonic"] == "csrrs" and name == "rd":
                #     v = 0
                # else:
                # TODO: do something smart here for compressed instructions
                # In 1 out of num_regs+1 cases provide random register
                if random.randint(0, num_regs) == 0:
                    v = random.randint(0, (1<<bits)-1)
                else:
                    v = random.randint(0, min(num_regs-1, (1<<bits)-1))
                set_val = v<<lsb
                              # TODO: immh and immb are special, see https://www.scs.stanford.edu/~zyedidia/arm64/sqshlu_advsimd.html
                # print(name, bin(set_val))
            elif name in immediates:
                imm = immediates[name][((msb, lsb),)]
                assert(imm.bits == bits)
                # print(name, imm.ranges[0], (msb, lsb))
                assert(imm.ranges[0] == (msb, lsb))
                v = imm.get_random()
                set_val = imm.bit_mask(v)
            elif "imm" in (name or ""):
                if name not in ["immh", "immb", "imm9h", "imm9l", "imm8h", "imm8l", "immlo", "immhi", "immr", "imms", "imm5b"]:
                    # print(f"jo jo immm {name} -------------------------------------------------")
                    assert("imm" not in (name or ""))
            else:
                v = random.getrandbits(bits)
                set_val = v<<lsb

            # print(bin(inverted_mask), name, bin(set_val))
            b |= (set_val&inverted_mask)

        #     # TODO: provide interesting values here
        #     # and if we provide triple, use the same regs
        #     if var.replace("lo", "hi") in immediates:
        #         if var.endswith("lo"):
        #             # print(var, insn["variable_fields"])
        #             # TODO: weird, for c_nzuimm6lo this assertion fails
        #             assert(var.replace("lo", "hi") in insn["variable_fields"])
        #             continue
        #         v = immediates[var].get_random()
        #         b |= immediates[var].bit_mask(v)
        #     elif any([r in var for r in ["rd", "rs", "fd", "fs", "vd", "vs"]]):
        #         (msb, lsb) = self.arg_lut[var]
        #         bits = msb-lsb+1
        #         assert(bits == 5 or bits == 3)
        #         if insn["mnemonic"] == "csrrs" and var == "rs1":
        #             v = 0
        #         else:
        #             v = random.randint(0, num_regs-1)
        #         b |= v<<lsb
        #     elif var == "csr":
        #         # only allow floating point operations
        #         # other csrs ruin the differential fuzzing
        #         (msb, lsb) = self.arg_lut[var]
        #         v = 0x003
        #         b |= v<<lsb
        #     else:
        #         (msb, lsb) = self.arg_lut[var]
        #         bits = msb-lsb+1
        #         v = random.getrandbits(bits)
        #         b |= v<<lsb
        #     # if verbose:
        #     #     print(f"{var}=0x{v:x}")

        # if b & 0x3 != 0x3:
        #     b |= 0x0001 << 16
        # # if verbose:
        # #     print(insn["mnemonic"], hex(b))

        instr.value = b

        return instr

def main():
    collection = ArmInstructionCollection(extensions=["sve"])
    print(collection.to_dict())

if __name__ == '__main__':
    main()
