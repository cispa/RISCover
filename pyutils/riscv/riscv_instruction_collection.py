#!/usr/bin/env python3
import os
import fnmatch
import json
from json import JSONDecodeError
import random

from pyutils.config import FRAMEWORK_ROOT
from pyutils.instruction_collection import InstructionCollection
from pyutils.immediate import SignedImmediate, UnsignedImmediate

# TODO: non twos
# TODO: also do this for regs, probably add super class with get_random_mask
immediates = {
    "imm12": UnsignedImmediate([(31,20)]),
    "imm12hi": UnsignedImmediate([(31, 25), (11, 7)])
}

class RiscvInstructionCollection(InstructionCollection):
    # Canonical list of globs that define all RISC-V extensions used to build the DB
    ALL_EXTENSION_GLOBS = ["rv*", "unratified/rv*", "custom/rv_thead"]
    @staticmethod
    def _load_db():
        db = os.path.join(os.path.dirname(__file__), 'instructions.json')
        try:
            with open(db, 'r') as f:
                return json.loads(f.read())
        except FileNotFoundError:
            print("Error: 'instructions.json' file not found. Generate it with riscv_opcodes_to_instruction_collection.py")
            exit(1)
        except JSONDecodeError:
            print(f"Error: {db} is no valid json. Regenerate it with riscv_opcodes_to_instruction_collection.py")
            exit(1)

    # relative to FRAMEWORK_ROOT
    def __init__(self, extensions, weights_file="pyutils/riscv/weighted_opcodes.json", remove_instructions=[]):
        self.weights_file = weights_file
        self.removed_instructions = set()

        self.extensions = extensions

        # We expect fully expanded extension names here (no globs)
        assert(all('*' not in x for x in extensions))

        try:
            db = os.path.join(os.path.dirname(__file__), 'instructions.json')
            with open(db, "r") as f:
                all_instructions = json.loads(f.read())
        except JSONDecodeError:
            print(f"Error: {db} is no valid json. You most likely did not pull git LFS files correctly yet or it is corrupted. Generate it with riscv_opcodes_to_instruction_collection.py")
            exit(1)
        except FileNotFoundError:
            print("Error: 'instructions.json' file not found. Generate it with riscv_opcodes_to_instruction_collection.py")
            exit(1)

        # Filter by provided extensions (intersection)
        self.instructions = {
            instr: details
            for instr, details in all_instructions.items()
            if any(ext in extensions for ext in details.get("extension", []))
        }

        # Remove instructions explicitly requested
        for i in remove_instructions:
            if i in self.instructions:
                del self.instructions[i]
                self.removed_instructions.add(i)

        if len(self.instructions) == 0:
            cmd = "riscv_opcodes_to_instruction_collection.py"
            raise Exception(f"No instructions generated from the specified extensions. Make sure they exist or match the glob you specified.\nYou used {', '.join(self.extensions)}\nThe command is {cmd}")

        with open(os.path.join(FRAMEWORK_ROOT, self.weights_file), "r") as f:
            self.weights = json.load(f)

        self.update_maps()

    def to_dict(self):
        d = super().to_dict()
        d["type"] = "RiscvInstructionCollection"
        d["weights_file"] = self.weights_file
        return d

    def randomly_init_instr(self, mnemonic, num_regs):
        instr = self.init_instr(mnemonic=mnemonic)

        insn = self.instructions[mnemonic]

        mask, value = insn["combined_mask"]
        b = instr.value

        # TODO: maybe store a list of variable fields
        # TODO TODO: yes do this, and then use it below in the assert(any...
        # make it an dict and improve extract_field_value above
        for var in insn["fields"]:

            range_end, range_start = var["range"]
            mask = var["mask"]
            name = var["name"]

            # TODO: best case we would use this mask, but riscv has no weird fields where only some bits are set
            # therefore it doesnt make a difference
            # inverted_mask = (~mask & ((1<<(range_end-range_start))-1)) << range_start
            if not name:
                continue

            msb, lsb = (range_end, range_start)

            # TODO: just skip fences for now (above)
            # check which fields get which values later
            # if (i == "fence_tso" or i == "fence") and (var == "rs1" or var == "rd"):
            #     # They should both be 0
            #     continue

            # TODO: provide interesting values here
            # and if we provide triple, use the same regs
            if name.replace("lo", "hi") in immediates:
                if name.endswith("lo"):
                    # print(name, insn["nameiable_fields"])
                    # TODO: weird, for c_nzuimm6lo this assertion fails
                    assert(any(map(lambda n: name.replace("lo", "hi") == n["name"], insn["fields"])))
                    continue
                v = immediates[name].get_random()
                b |= immediates[name].bit_mask(v)
            elif any([r in name for r in ["rd", "rs", "fd", "fs", "vd", "vs"]]):
                bits = msb-lsb+1
                assert(bits == 5 or bits == 3)
                if insn["mnemonic"] == "csrrs" and name == "rd":
                    v = 0
                else:
                    # TODO: do something smart here for compressed instructions
                    # In 1 out of num_regs+1 cases provide random register
                    if random.randint(0, num_regs) == 0:
                        v = random.randint(0, (1<<bits)-1)
                    else:
                        v = random.randint(0, min(num_regs-1, (1<<bits)-1))
                b |= v<<lsb
            # elif name == "csr":
            #     # only allow floating point operations
            #     # other csrs ruin the differential fuzzing
            #     v = 0x003
            #     b |= v<<lsb
            else:
                bits = msb-lsb+1
                v = random.getrandbits(bits)
                b |= v<<lsb
            # if verbose:
            #     print(f"{var}=0x{v:x}")

        if b & 0x3 != 0x3:
            b |= 0x0001 << 16
        # if verbose:
        #     print(insn["mnemonic"], hex(b))

        instr.value = b

        return instr

    # TODO: maybe return disassembledinsrtuction with disassemble?
    def extract_field_value(self, instr, d_instr, field):
        return self.extract_value(instr, *next(filter(lambda f: f["name"] == field, self.instructions[d_instr]["fields"]))["range"])

    @staticmethod
    def get_extensions_matching_globs(globs):
        return RiscvInstructionCollection.expand_globs(globs)

    @staticmethod
    def list_extensions():
        instructions = RiscvInstructionCollection._load_db()
        all_exts = set()
        for _, details in instructions.items():
            for ext in details.get('extension', []):
                all_exts.add(ext)
        return sorted(all_exts)

    @staticmethod
    def expand_globs(globs):
        all_exts = RiscvInstructionCollection.list_extensions()
        expanded = []
        for pattern in globs:
            expanded += [e for e in all_exts if fnmatch.fnmatch(e, pattern)]
        # De-duplicate preserving order
        seen = set()
        uniq = []
        for e in expanded:
            if e not in seen:
                seen.add(e)
                uniq.append(e)
        return uniq

    def disassemble_bytes(self, byte_seq):
        ops = []
        while len(byte_seq) > 2:
            instr = int.from_bytes(byte_seq[0:4], byteorder="little")
            op = self.disassemble(instr)
            if op:
                if instr & 0xffff == 0 or instr & 3 != 3:
                    # Compressed or unimp
                    # assert(op == "unimp" or op.startswith("c."))
                    # print(op, byte_seq[:2].hex())
                    byte_seq = byte_seq[2:]
                else:
                    # print(op, byte_seq[:4].hex())
                    byte_seq = byte_seq[4:]
            else:
                op = "unknown"
                # print(op, byte_seq[:2].hex())
                byte_seq = byte_seq[2:]
            ops += [op]
        return ops
