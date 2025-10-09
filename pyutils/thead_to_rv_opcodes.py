#!/usr/bin/env python3
import re
import os
import sys
import json5

# from thead_parser import parse_all, Instruction, Mask

def parse(content):
    reg_pattern = r"{reg:(.*)}\n\.\.\."
    reg_match = re.search(reg_pattern, content, re.DOTALL)

    if not reg_match:
        return None

    # insn = Instruction()
    # insn.others={}

    # syn_match = re.search(r"Synopsis::\n(.*?)\n", content, re.DOTALL)
    # if syn_match:
    #     insn.synopsis = syn_match.group(1)

    mne_match = re.search(r"Mnemonic::\n(.*?)\n", content, re.DOTALL)
    mnemonic = None
    if mne_match:
        mnemonic = mne_match.group(1)

    reg_text = reg_match.group(1)
    mod=reg_text.replace("bits", '"bits"').replace("name", '"name"').replace("attr", '"attr"')
    reg_dict = json5.loads(mod)

    return (mnemonic, reg_dict)

def parse_all():

    parsed=[]

    root=os.path.join(os.path.dirname(__file__), "thead-extension-spec")
    for item in os.listdir(root):
        p=os.path.join(root, item)
        if os.path.isdir(p):
            for pos in os.listdir(p):
                if pos.endswith(".adoc"):
                    pp=os.path.join(p, pos)
                    with open(pp, "r") as f:
                        res=parse(f.read())
                        if res:
                            parsed+=[res]

    return parsed

parsed = parse_all()

out_file = "rv_thead"

with open(out_file, "w") as f:
    for mne, instr in parsed:
        mne_short = mne.split(" ")[0]
        # TODO: these have broken overlapping imm
        if mne_short in ["th.ext", "th.extu"]:
            for part in instr:
                if part["name"] in ["imm1", "imm2"]:
                    part["name"] += "_2_thead"
        at = 0
        encoding = []
        for part in instr:
            # TODO: what did they do here?
            if part["name"] in ["imm2", "imm1", "rd1", "rd2", "fs1", "fd"]:
                part["name"] += "_thead"
            if type(part["name"]) == int:
                encoding += [f"{part['bits']+at-1}..{at}={part['name']}"]
            else:
                encoding += [part['name']]
            at += part["bits"]
            # print(part)
        out = " ".join([mne_short]+list(reversed(encoding)))
        f.write(out+"\n")

print(f"result in {out_file}")
