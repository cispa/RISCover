#!/usr/bin/env python3
import os
import glob
import yaml
import csv
import json

from pyutils.instruction_collection import InstructionCollection


def get_extensions_matching_globs(opcodes_dir: str, globs: list[str]) -> list[str]:
    extensions = []
    for pattern in globs:
        extensions.extend(glob.glob(os.path.join(opcodes_dir, pattern), recursive=True))
    return [os.path.relpath(f, start=opcodes_dir) for f in extensions]


def split_encoding_str(encoding: str):
    # E.g. '101011010111-----000-----1110111'
    # result: [(0, '1110111'), (12, '000'), ...]
    encodings = []
    current = ""
    current_start = 0
    for i, e in enumerate(reversed(encoding)):
        if e == '-':
            if current:
                encodings += [(current_start, current)]
                current = ""
        else:
            if current:
                current = e + current
            else:
                current_start = i
                current = e

    if current:
        encodings += [(current_start, current)]

    return encodings


def main():
    base_dir = os.path.dirname(__file__)
    opcodes_dir = os.path.join(base_dir, 'riscv-opcodes')

    # Ensure we include all extensions in the DB
    from pyutils.riscv.riscv_instruction_collection import RiscvInstructionCollection
    extensions = get_extensions_matching_globs(opcodes_dir, RiscvInstructionCollection.ALL_EXTENSION_GLOBS)

    target = "instructions.json"


    # Uncomment to generate vector 0.7.1 db
    # extensions = extensions+get_extensions_matching_globs(opcodes_dir, ["custom/rv_v_0.7.1"])
    # extensions.remove("rv_v")
    # extensions.remove("unratified/rv_zabha")
    # target = "instructions_rv_v_0.7.1.json"


    cmd = f"make -C {opcodes_dir} clean everything EXTENSIONS='{' '.join(extensions)}'"
    if os.system(f"{cmd} >/dev/null 2>&1") != 0:
        raise Exception(f"Couldn't init with cmd:\n{cmd}")

    with open(os.path.join(opcodes_dir, 'instr_dict.yaml'), 'r') as outfile:
        insns = {key.replace('_', '.'): value for key, value in yaml.safe_load(outfile).items()}
        for k in insns:
            insns[k]["variable_fields"] = [i.replace('_', '.') for i in insns[k]["variable_fields"]]

    # Build arg LUT
    arg_lut = {}
    with open(os.path.join(opcodes_dir, 'arg_lut.csv'), 'r') as f:
        csv_reader = csv.reader(f, skipinitialspace=True)
        for row in csv_reader:
            k = row[0].replace("_", ".")
            v = (int(row[1]), int(row[2]))
            arg_lut[k] = v

        # Additional fields not automatically parsed
        arg_lut['mop.r.t.30'] = (30, 30)
        arg_lut['mop.r.t.27.26'] = (27, 26)
        arg_lut['mop.r.t.21.20'] = (21, 20)
        arg_lut['mop.rr.t.30'] = (30, 30)
        arg_lut['mop.rr.t.27.26'] = (27, 26)
        arg_lut['c.mop.t'] = (10, 8)

    instructions = {}
    for mnemonic, values in insns.items():
        if mnemonic in instructions:
            continue

        instructions[mnemonic] = {
            "mnemonic": mnemonic,
            "extension": values["extension"],
            # NOTE: we don't use a dict here since some fields may not have a name
            "fields": []
        }

        for start, encoding in split_encoding_str(values["encoding"]):
            value = int(encoding, 2)
            mask = (1 << len(encoding)) - 1
            instructions[mnemonic]["fields"].append({
                "range": (len(encoding) + start - 1, start),
                "name": None,
                "value": value,
                "mask": mask
            })

        # Validate against provided mask/match
        value_hex = int(values["match"], 16)
        mask_hex = int(values["mask"], 16)
        combined_mask = InstructionCollection.gen_combined_mask(instructions[mnemonic])
        assert combined_mask == (mask_hex, value_hex)

        for field in values["variable_fields"]:
            msb, lsb = arg_lut[field]
            instructions[mnemonic]["fields"].append({
                # MSB and LSB index
                "range": (msb, lsb),
                "name": field,
                # 0 says everything can be set for these bits
                "value": 0,
                "mask": 0
            })

        combined_mask = InstructionCollection.gen_combined_mask(instructions[mnemonic])
        instructions[mnemonic]["combined_mask"] = combined_mask

    with open(os.path.join(base_dir, target), "w") as f:
        f.write(json.dumps(instructions))
        print("Wrote to "+target)


if __name__ == '__main__':
    main()
