#!/usr/bin/env python3
from pyutils.generation.generator import Generator
from pyutils.inp import Input

class UndocGenerator(Generator):
    def __init__(self, instruction_collection, instruction_filter):
        super().__init__(instruction_collection=instruction_collection)

        self.instruction_filter = instruction_filter

    def generate(self, counter: int, until: int) -> list[Input]:
        return [Input([instr]) for instr in self.instruction_collection.gen_unknown_instr_sequentially_until(counter, until, ifilter=self.instruction_filter)]

    def init(self, args):
        super().init(args)

    def to_dict(self) -> dict:
        d = super().to_dict()
        return d | {
            "instruction_filter": self.instruction_filter,
        }

    def parser_add_args(parser):
        pass
