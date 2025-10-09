#!/usr/bin/env python3
import random
import numpy as np
from typing import Iterator

from pyutils.generation.instruction import Instruction

def seed_all(seed):
    random.seed(seed % 2**32)
    np.random.seed(seed % 2**32)

def init_random_instructions(instruction_collection, n, num_regs, weighted=False) -> Iterator[Instruction]:
    if weighted:
        for r in random.choices(list(instruction_collection.weighted_instructions_cum.keys()), cum_weights=list(instruction_collection.weighted_instructions_cum.values()), k=n):
            yield instruction_collection.randomly_init_instr(mnemonic=r, num_regs=num_regs)
    else:
        for r in random.choices(list(instruction_collection.instructions.keys()), k=n):
            yield instruction_collection.randomly_init_instr(mnemonic=r, num_regs=num_regs)
