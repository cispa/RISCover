#!/usr/bin/env python3
import os
import re
import random
import xmltodict
import json

from pyutils.instruction_collection import InstructionCollection

instructions = {}

# TODO: not sure why we can't make clean instr_dict.yaml here
# when make instr_dict.yaml is used, rdcycle is not included, only when everything is build
# cmd = f"make -B -C mra_tools all"
# NOTE: there seems to be no way to exclude classes of instructions, e.g. all pauth instructions
# so for now just ignore them
cmd = f"make FILTER=--arch=AArch64 -C {os.path.join(os.path.dirname(__file__), 'mra_tools')} arch/arch.tag"
if os.system(f"{cmd} >/dev/null 2>&1") != 0:
    raise Exception(f"Couldn't init with cmd:\n{cmd}\n\nIs the mra_tools repo correctly initialized?")

with open(os.path.join(os.path.dirname(__file__), 'mra_tools/arch/arch.tag'), 'r') as tagfile:
    lines = tagfile.readlines()

    current_tag = None

    diagram_start = re.compile(r'TAG:\S+:diagram')
    current_line = 0
    while current_line < len(lines):
        line = lines[current_line]

        # Filter out undefined instructions here already
        if diagram_start.match(line) and "undef" not in line:
            current_tag = line.split(':')[1]
            ff = lines[current_line+1].strip()
            instructions[current_tag] = {
                "mnemonic": current_tag,
                # NOTE: we don't use a dict here since some fields may not have a name
                "fields": [],
                "ff": ff
            }
            current_line += 3
            while current_line < len(lines):
                line = lines[current_line]
                if line.startswith("TAG:"):
                    break
                parts = line.split()
                bit_range, name, value = parts

                range_end, range_start = map(int, bit_range.split(":"))

                # TODO: do something special with (xxxx)
                # these bascially indicate undefined behavior
                # https://alastairreid.github.io/mra_tools/
                # Replace everything in parantheses with x
                value = re.sub(r'\((.*?)\)', lambda m: 'x' * len(m.group(1)), value)

                mask = int(value.replace("0", "1").replace("x", "0"), 2)
                value = int(value.replace("x", "0"), 2)

                instructions[current_tag]["fields"].append({
                    "range": (range_end, range_start),
                    "name": name if name != "_" else None,
                    "value": value,
                    "mask": mask
                })

                current_line += 1

            combined_mask = InstructionCollection.gen_combined_mask(instructions[current_tag])
            instructions[current_tag]["combined_mask"] = combined_mask

        current_line += 1

march_dir=os.path.join(os.path.dirname(__file__), 'mra_tools/march/ISA_A64_xml_A_profile-2024-03')
for file_name in os.listdir(march_dir):
    if file_name.endswith('index.xml') and file_name != 'encodingindex.xml':
        match file_name:
            case 'index.xml':
                extension = "base"
            case 'fpsimdindex.xml':
                extension = "fpsimd"
            case 'mortlachindex.xml':
                extension = "sme"
            case 'sveindex.xml':
                extension = "sve"
            case _:
                raise ValueError(f"new extension? {file_name}")
        file_path = os.path.join(march_dir, file_name)
        with open(file_path, "r") as f:
            tree = xmltodict.parse(f.read())
        for test in tree["alphaindex"]["iforms"]["iform"]:
            mnemonic = test["@id"]
            description = test["#text"]
            for instr in list(key for key, inner_dict in instructions.items() if os.path.basename(inner_dict.get("ff")) == test["@iformfile"]):
                instructions[instr]["extension"] = extension
                instructions[instr]["mnemonic"] = mnemonic
                instructions[instr]["description"] = description

with open("instructions.json", "w") as f:
    f.write(json.dumps(instructions))
    print("Wrote to instructions.json")
