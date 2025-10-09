#!/usr/bin/env python3
import struct
from abc import abstractmethod
import yaml

import pyutils.config as config

from pyutils.disassembly import disasm_capstone, disasm_opcodes
from pyutils.assembly import asm_opcodes
from pyutils.instruction_collection import InstructionCollection
from pyutils.generation.instruction import Instruction
from pyutils.util import VEC_REG_SIZE, gp, fp, vec, regs_mapping, repeat_int64, hex_yaml

match config.ARCH:
    case "riscv64":
        from pyutils.riscv.fuzzing_value_map import fuzzing_value_map_gp, fuzzing_value_map_fp, filler_64
    case "aarch64":
        from pyutils.arm.fuzzing_value_map import fuzzing_value_map_gp, fuzzing_value_map_fp, filler_64

def to_input_with_values_helper(instr_seq, seq_len, _gp, _fp, _vec):
    _gp = expand_regs_gp(_gp, filler_64)

    if _fp != None:
        _fp = expand_regs_fp(_fp, filler_64)

    if _vec != None:
        assert(VEC_REG_SIZE % 8 == 0)
        _vec = expand_regs_vec(_vec, repeat_int64(filler_64, VEC_REG_SIZE//8))

    match config.ARCH:
        case "riscv64":
            return InputWithValuesRiscv64(_gp=_gp, instr_seq=instr_seq, seq_len=seq_len, _fp=_fp, _vec=_vec)
        case "aarch64":
            return InputWithValuesAarch64(_gp=_gp, instr_seq=instr_seq, seq_len=seq_len, _fp=_fp, _vec=_vec)

class Input:
    def __init__(self, seq_len):
        self.seq_len = seq_len
        self.full_seq = False

    @abstractmethod
    def pack(self) -> bytes:
        pass

    @abstractmethod
    def to_input_with_values(self):
        pass

    @abstractmethod
    def get_num_results(self):
        pass

class InputJustSeqNum(Input):
    def __init__(self, seq_num, n, seq_len, generator):
        super().__init__(seq_len)
        self.seq_num = seq_num
        self.n = n
        self.generator = generator

    def get_num_results(self):
        return self.n

    def to_input_with_values(self):
        return self.generator.get_real_input(self)

    def pack(self) -> bytes:
        return struct.pack("QHBB", *[self.seq_num, self.n, self.seq_len, self.full_seq])

class FullInput(Input):
    def __init__(self, instr_seq, seq_len, dis_capstone=None, dis_opcodes=None, dis_mra=None):
        super().__init__(seq_len=seq_len)
        # NOTE: prefixed with _ because field should not be accessed directly
        # If its used the seq_len of the input has to be copied to
        self._instr_seq = instr_seq
        self.dis_capstone = dis_capstone
        self.dis_opcodes = dis_opcodes
        self.dis_mra = dis_mra

    def get_num_results(self):
        return 1

    def get_format_string(self) -> str:
        return f"BB{self.seq_len}I"

    def get_instr_seq(self):
        return self._instr_seq[:self.seq_len]

    def get_pack_args(self):
        return [self.seq_len, self.full_seq, *self.get_instr_seq()]

    def pack(self) -> bytes:
        assert(self.seq_len > 0)
        return struct.pack(self.get_format_string(), *self.get_pack_args())

    @staticmethod
    def shared_parse_dict(d):
        if "instr_seq" not in d:
            # assemble instructions
            assert("dis_opcodes" in d)
            instr_seq = [x for i in d["dis_opcodes"] for x in asm_opcodes(i)]
        else:
            instr_seq = d["instr_seq"]

        if "dis_opcodes" in d:
            dis_opcodes = d["dis_opcodes"]
        else:
            dis_opcodes = None
        if "dis_capstone" in d:
            dis_capstone = d["dis_capstone"]
        else:
            dis_capstone = None
        if "dis_mra" in d:
            dis_mra = d["dis_mra"]
        else:
            dis_mra = None

        return instr_seq, len(instr_seq), dis_capstone, dis_opcodes, dis_mra

    @classmethod
    def from_str(cls, txt):
        return cls.from_dict(yaml.safe_load(txt)["input"])

    @classmethod
    def from_file(cls, path):
        with open(path, 'r') as f:
            return cls.from_dict(yaml.safe_load(f)["input"])

    @classmethod
    def from_dict(cls, d):
        instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra = cls.shared_parse_dict(d)
        return cls(instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra)

    def disasm_capstone(self):
        return list(disasm_capstone(self.get_instr_seq()))

    def disasm_opcodes(self):
        return list(disasm_opcodes(self.get_instr_seq()))

    def disasm_mra(self, instruction_collection):
        return list(instruction_collection.disassemble_sequence(self.get_instr_seq()))

    def fill_verbose(self, instruction_collection):
        self._instructions = []
        for instr in self.get_instr_seq():
            mnemonic = instruction_collection.disassemble(instr)
            mra_i = instruction_collection.instructions[mnemonic]
            self._instructions += [Instruction(mnemonic, mra_i, instr)]
        self.regs = self.decode_regs()

    def get_instructions(self):
        return self._instructions[:self.seq_len]

    def to_input_with_values(self):
        # TODO: is that value actually used when undoc fuzzing?
        return to_input_with_values_helper(self._instr_seq, self.seq_len, {}, {} if config.FLOATS else None, {} if config.VECTOR else None)

class InputWithRegSelect(FullInput):
    input_with_reg_select_bytes = len(gp)
    if config.FLOATS:
        input_with_reg_select_bytes += len(fp)
    if config.VECTOR:
        input_with_reg_select_bytes += len(vec)*VEC_REG_SIZE//8

    # TODO: is this super slow?
    # NOTE: ... + padding in struct (currently 0), see diffuzz-client.c
    format_string=f'{input_with_reg_select_bytes}B{0}B'

    def __init__(self, reg_select_gp, instr_seq, reg_select_fp=None, reg_select_vec=None, instructions=None):
        super().__init__(instr_seq, seq_len=len(instr_seq))
        self.reg_select_gp = reg_select_gp
        self.reg_select_fp = reg_select_fp
        self.reg_select_vec = reg_select_vec
        self._instructions = instructions

    def to_input_with_values(self):
        initial_regs = self.decode_regs()
        _gp = [initial_regs[j] for j in gp]
        if fp[0] in initial_regs:
            _fp = [initial_regs[j] for j in fp]
        else:
            _fp = None
        if vec[0] in initial_regs:
            _vec = [initial_regs[j] for j in vec]
        else:
            _vec = None

        match config.ARCH:
            case "riscv64":
                return InputWithValuesRiscv64(_gp=_gp, instr_seq=self._instr_seq, seq_len=self.seq_len, _fp=_fp, _vec=_vec)
            case "aarch64":
                return InputWithValuesAarch64(_gp=_gp, instr_seq=self._instr_seq, seq_len=self.seq_len, _fp=_fp, _vec=_vec)

    def pack(self) -> bytes:
        super_format_string = super().get_format_string()
        super_args = super().get_pack_args()
        if self.reg_select_vec is not None and self.reg_select_fp is not None:
            return struct.pack(self.format_string+super_format_string, *[*self.reg_select_gp, *self.reg_select_fp, *self.reg_select_vec, *super_args])
        elif self.reg_select_fp is not None:
            return struct.pack(self.format_string+super_format_string, *[*self.reg_select_gp, *self.reg_select_fp, *super_args])
        elif self.reg_select_vec is not None:
            return struct.pack(self.format_string+super_format_string, *[*self.reg_select_gp, *self.reg_select_vec, *super_args])
        else:
            return struct.pack(self.format_string+super_format_string, *[*self.reg_select_gp, *super_args])

    def decode_regs(self):
        regs = {}
        for j, r in zip(self.reg_select_gp, gp):
            regs[r]=fuzzing_value_map_gp[j]
        if self.reg_select_fp is not None:
            for j, r in zip(self.reg_select_fp, fp):
                regs[r]=fuzzing_value_map_fp[j]
        if self.reg_select_vec is not None:
            for i, v in enumerate(vec):
                regs[v]=0
                for j in reversed(self.reg_select_vec[i*VEC_REG_SIZE//8:(i+1)*VEC_REG_SIZE//8]):
                    regs[v] = (regs[v] << 64) | fuzzing_value_map_gp[j]
        match config.ARCH:
            case "riscv64":
                # We zero that out on the client side
                regs["fcsr"] = 0
            case "aarch64":
                # We zero that out on the client side
                regs["pstate"] = 0
                regs["fpsr"] = 0
        return regs

    def print(self, instruction_collection):
        dis_capstone = self.disasm_capstone()
        dis_opcodes = self.disasm_opcodes()
        dis_mra = self.disasm_mra(instruction_collection)

        print(" ".join([f"0x{x:08x}" for x in self.get_instr_seq()]))
        print("dis cap", dis_capstone)
        print("dis opcodes", dis_opcodes)
        print("dis mra", list(dis_mra))

class InputWithValues(FullInput):
    def __init__(self, _gp, instr_seq, seq_len, _fp=None, _vec=None, dis_capstone=None, dis_opcodes=None, dis_mra=None):
        super().__init__(instr_seq, seq_len, dis_capstone=dis_capstone, dis_opcodes=dis_opcodes, dis_mra=dis_mra)

        self.gp = _gp
        assert(len(self.gp) == len(gp))

        self.fp = None
        if _fp:
            self.fp = _fp
            assert(len(self.fp) == len(fp))

        self.vec = None
        if _vec:
            self.vec = _vec
            assert(len(self.vec) == len(vec))

        self.dis_capstone = dis_capstone
        self.dis_opcodes = dis_opcodes
        self.dis_mra = dis_mra

    # TODO: prepend fcsr too, then we can simplify that
    def prepare_format_string(self, gp_append="", fp_append="", fp_prepend="", fp_vec_merged=False):
        format_string=f'{len(gp)}Q{gp_append}'
        format_string+=fp_prepend
        if self.fp:
            if not (self.vec and fp_vec_merged):
                format_string+=f'{len(fp)}Q'
        format_string+=fp_append
        if self.vec:
            format_string+=f'{len(self.vec)*VEC_REG_SIZE}B'
        return format_string

    def decode_regs(self):
        regs = {}
        for j, r in zip(self.gp, gp):
            regs[r]=j
        if self.fp:
            for j, r in zip(self.fp, fp):
                regs[r]=j
        if self.vec:
            for j, r in zip(self.vec, vec):
                regs[r]=j
        return regs

    def to_dict(self, instruction_collection) -> dict:
        d={
            "instr_seq": self.get_instr_seq(),
            "dis_opcodes": self.disasm_opcodes() if not self.dis_opcodes else self.dis_opcodes[:self.seq_len],
            "dis_capstone": self.disasm_capstone() if not self.dis_capstone else self.dis_capstone[:self.seq_len],
            "dis_mra": self.disasm_mra(instruction_collection) if not self.dis_mra else self.dis_mra[:self.seq_len],
        }

        regs = {}
        regs["gp"] = {j: v for j, v in zip(gp, self.gp)}
        if self.fp:
            regs["fp"] = {j: v for j, v in zip(fp, self.fp)}
        if self.vec:
            regs["vec"] = {j: v for j, v in zip(vec, self.vec)}

        d["regs"] = regs

        return d

    def to_yaml(self, instruction_collection):
        return hex_yaml(self.to_dict(instruction_collection))

    @staticmethod
    def from_dict(d) -> dict:
        match config.ARCH:
            case "riscv64":
                return InputWithValuesRiscv64.from_dict(d)
            case "aarch64":
                return InputWithValuesAarch64.from_dict(d)

    @staticmethod
    def shared_parse_dict(d):
        instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra = FullInput.shared_parse_dict(d)

        regs = d["regs"]
        _gp = [regs["gp"][j] for j in gp]
        if config.FLOATS:
            _fp = [regs["fp"][j] for j in fp]
        else:
            _fp = None
        if config.VECTOR:
            _vec = [regs["vec"][j] for j in vec]
        else:
            _vec = None

        return _gp, _fp, _vec, instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra

    # TODO
    @classmethod
    def repro_to_batch(cls, repro):
        return cls.from_dict(repro["regs"], repro["instr_seq"])

    def to_input_with_values(self):
        return self


# TODO: we could merge these if we would do sparse struct regs
class InputWithValuesRiscv64(InputWithValues):
    format_map = {}

    def __init__(self, _gp, instr_seq, seq_len, _fp=None, _vec=None, dis_capstone=None, dis_opcodes=None, dis_mra=None):
        super().__init__(_gp, instr_seq, seq_len, _fp, _vec, dis_capstone, dis_opcodes, dis_mra)

        if _vec:
            assert(self.fp)

        self.format_string = self.get_format_string()

    def get_format_string(self):
        lookup = (self.fp == None, self.vec == None)
        if lookup not in self.format_map:
            if self.fp:
                self.format_map[lookup] = super().prepare_format_string(fp_append="1Q")
            else:
                self.format_map[lookup] = super().prepare_format_string()
        return self.format_map[lookup]

    def pack(self) -> bytes:
        super_format_string = super().get_format_string()
        super_args = super().get_pack_args()
        if not self.fp:
            return struct.pack(self.format_string+super_format_string, *[self.gp, *super_args])
        else:
            if not self.vec:
                return struct.pack(self.format_string+super_format_string, *[*self.gp, *self.fp, 0, *super_args])
            else:
                return struct.pack(self.format_string+super_format_string, *[*self.gp, *self.fp, 0, *(b"".join([x.to_bytes(VEC_REG_SIZE, "little") for x in self.vec])), *super_args])

    def decode_regs(self):
        regs = super().decode_regs()
        # We zero that out on the client side
        regs["fcsr"] = 0
        return regs

    @classmethod
    def from_dict(cls, d):
        _gp, _fp, _vec, instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra = super().shared_parse_dict(d)

        return cls(_gp=_gp, instr_seq=instr_seq, seq_len=seq_len, _fp=_fp, _vec=_vec, dis_capstone=dis_capstone, dis_opcodes=dis_opcodes, dis_mra=dis_mra)


class InputWithValuesAarch64(InputWithValues):
    format_map = {}

    def __init__(self, _gp, instr_seq, seq_len, _fp=None, _vec=None, dis_capstone=None, dis_opcodes=None, dis_mra=None):
        super().__init__(_gp, instr_seq, seq_len, _fp, _vec, dis_capstone, dis_opcodes, dis_mra)

        assert(not _fp or not _vec)

        self.format_string = self.get_format_string()

    def get_format_string(self):
        lookup = (self.fp == None, self.vec == None)
        if lookup not in self.format_map:
            if self.fp or self.vec:
                self.format_map[lookup] = super().prepare_format_string(gp_append="1Q", fp_prepend="1Q", fp_vec_merged=True)
            else:
                self.format_map[lookup] = super().prepare_format_string(gp_append="1Q", fp_vec_merged=True)
        return self.format_map[lookup]

    def pack(self) -> bytes:
        super_format_string = super().get_format_string()
        super_args = super().get_pack_args()
        if self.fp:
            return struct.pack(self.format_string+super_format_string, *[*self.gp, 0, 0, *self.fp, *super_args])
        elif self.vec:
            return struct.pack(self.format_string+super_format_string, *[*self.gp, 0, 0, *(b"".join([x.to_bytes(VEC_REG_SIZE, "little") for x in self.vec])), *super_args])
        else:
            return struct.pack(self.format_string+super_format_string, *[*self.gp, 0, *super_args])

    def decode_regs(self):
        regs = super().decode_regs()
        # We zero that out on the client side
        regs["pstate"] = 0
        if self.fp or self.vec:
            regs["fpsr"] = 0
        return regs

    def to_dict(self, instruction_collection) -> dict:
        return super().to_dict(instruction_collection)

    @classmethod
    def from_dict(cls, d):
        _gp, _fp, _vec, instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra = super().shared_parse_dict(d)

        return cls(_gp=_gp, instr_seq=instr_seq, seq_len=seq_len, _fp=_fp, _vec=_vec, dis_capstone=dis_capstone, dis_opcodes=dis_opcodes, dis_mra=dis_mra)


class InputWithSparseValues(FullInput):
    def __init__(self, instr_seq, seq_len, _gp={}, _fp=None, _vec=None, dis_capstone=None, dis_opcodes=None, dis_mra=None):
        super().__init__(instr_seq, seq_len, dis_capstone=dis_capstone, dis_opcodes=dis_opcodes, dis_mra=dis_mra)

        self.gp = _gp

        # Change the default value from None to {} depending on the config
        if _fp == None and config.FLOATS:
            _fp = {}
        self.fp = _fp

        # Change the default value from None to {} depending on the config
        if _vec == None and config.VECTOR:
            _vec = {}
        self.vec = _vec

    @classmethod
    def from_args(cls, args, instr_seq):
        inp = cls(instr_seq, len(instr_seq))
        for reg_arg in args.reg:
            reg_name, reg_value = reg_arg.split('=')
            if reg_name in gp:
                inp.gp[reg_name]=int(reg_value, 0)
            elif reg_name in fp:
                if inp.fp == None:
                    print("You specified a floating point register without the --floats flag.")
                    exit(1)
                inp.fp[reg_name]=int(reg_value, 0)
            elif reg_name in vec:
                if inp.vec == None:
                    print("You specified a vector register without the --vector flag.")
                    exit(1)
                inp.vec[reg_name]=int(reg_value, 0)
            else:
                print(f"Register {reg_name} can't be set. Maybe use the ABI name not x..?")
                exit(1)

        return inp

    @classmethod
    def from_dict(cls, d):
        instr_seq, seq_len, dis_capstone, dis_opcodes, dis_mra = super().shared_parse_dict(d)

        inp = cls(instr_seq=instr_seq, seq_len=seq_len, dis_capstone=dis_capstone, dis_opcodes=dis_opcodes, dis_mra=dis_mra)

        if "regs" in d:
            regs = d["regs"]
            if "gp" in regs:
                inp.gp.update(regs["gp"])
                regs.pop("gp")
            if "fp" in regs:
                inp.fp.update(regs["fp"])
                regs.pop("fp")
            if "vec" in regs:
                inp.vec.update(regs["vec"])
                regs.pop("vec")

        return inp

    def to_input_with_values(self) -> InputWithValues:
        return to_input_with_values_helper(self._instr_seq, self.seq_num, self.gp, self.fp, self.vec)


def expand_regs(regs, fill_val, all_regs):
    l = []
    assert(all([reg in all_regs for reg in regs]))
    for reg in all_regs:
        if reg in regs:
            l += [regs[reg]]
        else:
            l += [fill_val]
    return l

def expand_regs_gp(regs, fill_val):
    return expand_regs(regs, fill_val, gp)

def expand_regs_fp(regs, fill_val):
    return expand_regs(regs, fill_val, fp)

def expand_regs_vec(regs, fill_val):
    return expand_regs(regs, fill_val, vec)

def pack_inputs(inputs: list[Input]) -> bytes:
    return b"".join([inp.pack() for inp in inputs])
