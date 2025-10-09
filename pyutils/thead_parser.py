#!/usr/bin/env python3
import os
import re
import json5

reg_mapping = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]

def gen_mask(bits, shift):
    return ((1<<bits)-1)<<shift

class Mask:
    def __init__(self, bits=None, shift=None, val=None):
        self.bits = bits
        self.shift = shift
        self.mask = 0
        if self.shift != None and self.bits != None:
            self.mask = gen_mask(self.bits, self.shift)
        self.val = 0
        if self.shift != None and val != None:
            self.val = val << self.shift
        self.attr = None

    def add(self, bits, shift, val):
        self.shift = None
        self.bits = None
        self.val |= val << shift
        self.mask |= gen_mask(bits, shift)
        return self

    def matches(self, v):
        return (v & self.mask) == self.val

    def extract(self, v):
        return (v & self.mask) >> self.shift


class Instruction:
    opcode = None
    opcode_name = ""

    mask = None

    further_constraints = []

    others = {}

    val = None

    def init(opcode, mask, mnemonic, others, further_constraints=[]):
        i = Instruction()
        i.opcode = opcode
        i.mask = mask
        i.mnemonic = mnemonic
        i.others = others
        i.further_constraints = further_constraints
        return i

    def __str__(self):
        if self.val:
            out=self.mnemonic
            for mask_key in self.others:
                mask=self.others[mask_key]
                val=mask.extract(self.val)
                if "rd" in mask_key or "rs" in mask_key:
                    v=reg_mapping[val]
                else:
                    v=hex(val)
                out=out.replace("_"+mask_key+"_", v)
            return out
        else:
            return self.mnemonic

def parse(content):
    reg_pattern = r"{reg:(.*)}\n\.\.\."
    reg_match = re.search(reg_pattern, content, re.DOTALL)

    if not reg_match:
        return None

    insn = Instruction()
    insn.others={}

    syn_match = re.search(r"Synopsis::\n(.*?)\n", content, re.DOTALL)
    if syn_match:
        insn.synopsis = syn_match.group(1)

    mne_match = re.search(r"Mnemonic::\n(.*?)\n", content, re.DOTALL)
    if mne_match:
        insn.mnemonic = mne_match.group(1)

    reg_text = reg_match.group(1)
    mod=reg_text.replace("bits", '"bits"').replace("name", '"name"').replace("attr", '"attr"')
    reg_dict = json5.loads(mod)

    assert(reg_dict[0]["bits"] == 7)
    insn.opcode = Mask(reg_dict[0]["bits"], 0, reg_dict[0]["name"])
    insn.opcode_name = "+".join(reg_dict[0]["attr"])

    j = 7

    m = Mask()
    for c in reg_dict[1:]:
        bits=c["bits"]
        if type(c["name"]) == str:
            insn.others[c["name"]]=Mask(bits, j)
            j+=bits
            if "attr" in c:
                insn.others[c["name"]].attr=c["attr"]
        else:
            m.add(bits, j, c["name"])
            j+=c["bits"]
    insn.mask = m

    assert(j==32)

    # TODO: other costraints?

    # print(insn.opcode, bin(insn.mask), bin(insn.val), insn.mnemonic, insn.synopsis)
    # print("others:", insn.others)

    return insn

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
