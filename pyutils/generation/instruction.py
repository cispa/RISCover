#!/usr/bin/env python3
from tabulate import tabulate

from pyutils.util import bcolors, gp, fp, is_floating_point_instr, is_memory_read, is_memory_write

class Instruction():
    def __init__(self, mnemonic, mra_instruction, value):
        self.mnemonic = mnemonic
        self.mra_instruction = mra_instruction
        self.value = value

    def __getattr__(self, name):
        fields = list(filter(lambda n: n, map(lambda v: v["name"], self.mra_instruction["fields"])))
        if name in fields:
            f = next(filter(lambda x: x["name"] == name, self.mra_instruction["fields"]))
            range_end, range_start = f["range"]
            bits = range_end - range_start + 1
            return self.extract_value(range_end, range_start)
        raise AttributeError(f"No field with name {name}. Possible fields: {', '.join(fields)}")

    def __setattr__(self, name, value):
        if name in ("mnemonic", "mra_instruction", "value"):
            return object.__setattr__(self, name, value)
        fields = list(filter(lambda n: n, map(lambda v: v["name"], self.mra_instruction["fields"])))
        if name in fields:
            self.set_field(name, value)
            return
        raise AttributeError(f"No field with name {name}. Possible fields: {', '.join(fields)}")

    def pack(self):
        return self.value

    def set_field(self, field_name, new_val, no_match_fine=False, mnemonic=None):
        instr_info = self.mra_instruction
        matched_fields = list(filter(lambda field: field["name"] == field_name, instr_info["fields"]))
        if not matched_fields:
            if no_match_fine:
                return
            else:
                raise RuntimeError(f"Field {field_name} not found on instruction {mnemonic}. Available fields: "+', '.join(filter(lambda n: n != None, [field["name"] for field in instr_info["fields"]])))
        assert(len(matched_fields) == 1)
        field = matched_fields[0]
        range_end, range_start = field["range"]
        assert(new_val < (1<<(range_end-range_start+1)))

        self.set_value(range_end, range_start, new_val)

    def extract_fields(self):
        fields = []
        instr_info = self.mra_instruction
        for field in instr_info["fields"]:
            range_end, range_start = field["range"]
            bits = range_end - range_start + 1
            val = self.extract_value(range_end, range_start)
            name = field["name"]
            # TODO(aarch64)
            # TODO(riscv64): add others
            # TODO: should we split this between architectures, its bad when something overlaps
            verbose = None
            if name in ["rd", "rs1", "rs2"]:
                if is_floating_point_instr(self.mnemonic) and not (is_memory_read(self.mnemonic) or is_memory_write(self.mnemonic)):
                    verbose = fp[val]
                else:
                    verbose = (["x0"]+gp)[val]
            fields += [(range_start, (name, val, verbose, bits))]
        return list(map(lambda x: x[1], sorted(fields)))

    def extract_value(self, msb, lsb):
        bits = msb-lsb+1
        v = (self.value >> lsb) & ((2**bits)-1)
        return v

    def set_value(self, msb, lsb, val):
        bits = msb-lsb+1
        assert(val < (1<<bits))
        unset_mask = ~(((1<<bits)-1) << lsb)
        self.value = (self.value & unset_mask) | (val << lsb)

    def __str__(self):
        fields = [
            f"{name}={hex(val)}"
            for name, val, _, _ in self.extract_fields()
        ]
        return f"{self.mnemonic} ({', '.join(fields)})"

    def pretty_print_instr(self):
        # if not dis:
        #     print(f"{bcolors.FAIL}??????????????????????????????????????:{bcolors.ENDC}")
        #     # dis = "A64.dpimm.addsub_imm.ADD_32_addsub_imm"
        #     exit(1)
        # else:
        print(f"{bcolors.OKGREEN}{self.mnemonic}:{bcolors.ENDC}")

        fields = self.extract_fields()
        table = []
        for (name, val, verbose_val, bits) in reversed(fields):
            table.append([
                f"{bcolors.HEADER}{name or ''}{bcolors.ENDC}",
                f"{bcolors.OKBLUE}{format(val, f'0{bits}b')}{bcolors.ENDC}",
                f"{bcolors.OKCYAN}{val}{bcolors.ENDC}",
                f"{bcolors.OKBLUE}{hex(val)}{bcolors.ENDC}",
                f"{bcolors.OKCYAN}{verbose_val or ''}{bcolors.ENDC}"
            ])

        table = [list(row) for row in zip(*table)]
        print(tabulate(table, tablefmt="plain", stralign="right"))
