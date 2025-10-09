#!/usr/bin/env python3
from abc import ABC, abstractmethod
from collections import defaultdict
from scipy.stats.mstats import winsorize
import numpy as np
from math import log
from itertools import accumulate
import git

from pyutils.util import bcolors, gp, fp, is_floating_point_instr, is_memory_read, is_memory_write
from pyutils.generation.instruction import Instruction

class InstructionCollection(ABC):
    def to_dict(self):
        return {
            "extensions": self.extensions,
            "removed_instructions": list(self.removed_instructions),
            "commit": git.Repo(search_parent_directories=True).head.object.hexsha
        }

    def update_maps(self):
        # Sort first so that we are deterministic
        self.instructions = dict(sorted(self.instructions.items()))

        ################ Generate mask map and extension grouping

        # NOTE: no defaultdict here since we want every bit to have a value
        unused_sums = {}
        for bit in range(32):
            unused_sums[bit] = 0
            for i in self.instructions:
                instr = self.instructions[i]
                mask, value = instr["combined_mask"]
                # If bit is unused (=not in mask)
                if (mask & (1 << bit)) >> bit == 0:
                    unused_sums[bit]+=1


        # NOTE: 11 is fast to compute and gives good performance
        # performance drops at around 14, but 12 and 13 aren't much higher
        n = 11
        most_interesting_bits = list(sorted(unused_sums, key=lambda dict_key: abs(unused_sums[dict_key])))[:n]

        def generate_combinations(bit_positions):
            def powerset(iterable):
                from itertools import chain, combinations
                return chain.from_iterable(combinations(iterable, r) for r in range(len(iterable)+1))

            c = []
            for subset in powerset(bit_positions):
                number = 0
                for bit in subset:
                    number |= 1 << bit
                c += [number]
            return c

        # All combinations of these bits set
        bit_combinations = generate_combinations(most_interesting_bits)

        self.abs_mask = 0
        for b in most_interesting_bits:
            self.abs_mask |= 1 << b

        self.mask_map = defaultdict(dict)
        for i in self.instructions:
            instr = self.instructions[i]
            mask, value = instr["combined_mask"]
            for comb in bit_combinations:
                comb = comb & ~mask
                m = (value | comb) & self.abs_mask
                d = self.mask_map[m]
                if mask not in d:
                    d[mask] = {}
                if not (value not in d[mask] or d[mask][value] == i):
                    # TODO: c.ld and c.flw seem to share encoding
                    if i == "c.ld" or "c.ldsp":
                        pass
                        # continue
                    else:
                        assert(False)
                d[mask][value] = i

        self.instr_per_extension = defaultdict(list)
        for i, a in self.instructions.items():
            for ext in a["extension"]:
                self.instr_per_extension[ext] += [i]

        # Sort the mask map by bit_count of the mask. This ensures that overlapping opcodes
        # are decoded correctly. E.g.:
        # print(self.disassemble(0x9102)) # c.jalr
        # print(self.disassemble(0x9002)) # c.ebreak
        sorted_mask_map = {}
        for kk, vv in self.mask_map.items():
            sorted_mask_map[kk] = {}
            for k, v in reversed(sorted(vv.items(), key=lambda item: item[0].bit_count())):
                sorted_mask_map[kk][k] = v
        self.mask_map = sorted_mask_map

        # Generate weighted map
        if self.weights:
            self.weighted_instructions = {}
            # Copy over existing weights
            for i in self.instructions:
                if i in self.weights:
                    self.weighted_instructions[i] = self.weights[i]

            if len(self.weighted_instructions) > 0:
                # Only keep the weights for the upper half of the instructions. Reasoning: we don't want to entirely kill high-frequent instructions, just reduce their
                # weight a bit.
                self.weighted_instructions = dict(zip(self.weighted_instructions.keys(), winsorize(np.array(list(self.weighted_instructions.values())), limits=(0.5, 0))))

                min_weight = min(self.weighted_instructions.values())
                max_weight = max(self.weighted_instructions.values())
                if min_weight == max_weight:
                    max_weight += 1
            else:
                min_weight = 1
                max_weight = 2

            # Initialize unseen instructions with minimum
            for i in self.instructions:
                if i not in self.weighted_instructions:
                    self.weighted_instructions[i] = min_weight

            # Invert and compress weights so that factor*min=max
            factor = 10
            # max^a=x*min^a
            # max=x^(1/a)*min
            # log(max/min, x)=1/a
            # a=1/log(max/min, x)
            for i in self.instructions:
                self.weighted_instructions[i] = (max_weight/self.weighted_instructions[i]) ** (1./log(max_weight/min_weight, factor))

            # Precompute cumulative weights
            self.weighted_instructions_cum = dict(zip(self.weighted_instructions.keys(), accumulate(self.weighted_instructions.values())))

    def remove_instructions(self, mnemonics):
        self.removed_instructions.update(mnemonics)

        for m in mnemonics:
            del self.instructions[m]

        self.update_maps()

    def disassemble(self, instr):
        assert(instr < (1<<32))
        abs_m = instr & self.abs_mask
        # If we never encoded such a mask, the instruction cant disasm at all
        # TODO: think about this again
        if abs_m in self.mask_map:
            for mask, mapping in self.mask_map[abs_m].items():
                val = instr & mask
                if val in mapping:
                    return mapping[val]
        return None

    def assemble_static_fields(self, mnemonic):
        if mnemonic in self.instructions:
            instr = self.instructions[mnemonic]
            return instr["combined_mask"][1]
        else:
            h="Available mnemonics:\n"+"\n".join(self.instructions)
            import os, subprocess
            pager = os.getenv("PAGER", "less")
            with subprocess.Popen([pager], stdin=subprocess.PIPE) as proc:
                proc.communicate(input=h.encode("utf-8"))
            exit(1)

    @staticmethod
    def gen_combined_mask(instr: dict):
        mask = 0
        value = 0
        for field in instr["fields"]:
            _, range_start = field["range"]
            mask |= field["mask"] << range_start
            value |= field["value"] << range_start
        assert(mask & value == value)
        return mask, value

    def disassemble_sequence(self, seq):
        return map(self.disassemble, seq)

    def gen_unknown_instr_sequentially_until(self, start_at: int, until: int, ifilter=None) -> list[int]:
        l = []
        while start_at < until:
            # TODO: 16-bit thumb instructions?
            d = self.disassemble(start_at)
            if not d and (not ifilter or not ifilter(start_at)):
                l += [start_at]
            start_at += 1
        return l

    def init_instr(self, mnemonic):
        insn = self.instructions[mnemonic]
        _, value = insn["combined_mask"]
        return Instruction(mnemonic=mnemonic, mra_instruction=insn, value=value)
