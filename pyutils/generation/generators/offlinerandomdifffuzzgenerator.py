#!/usr/bin/env python3
if __name__ == '__main__':
    import sys, os
    sys.path.append(os.getenv("FRAMEWORK_ROOT"))

###############################################################################

import subprocess
import os

from pyutils.inp import InputWithValues, InputJustSeqNum
from pyutils.generation.generators.randomdifffuzzgenerator import RandomDiffFuzzGenerator
from pyutils.generation.generators.genheader import write_header

class OfflineRandomDiffFuzzGenerator(RandomDiffFuzzGenerator):
    def __init__(self, instruction_collection, seq_len, num_regs, seed, weighted):
        super().__init__(instruction_collection, seq_len, num_regs, seed, weighted, compress_send=False)

        self.input_generator_path = os.path.join(os.path.dirname(__file__), 'input-generator')
        self.late_init_success = False
        self.expand_inputs_after_exec = True

    def early_init(self, build_flags):
        write_header(self.instruction_collection, name="filtered_instructions.h")

    def late_init(self, build_flags):
        result = subprocess.run(["build-client", "--target", "input-generator", "--out", self.input_generator_path, "--build-flags", " ".join(build_flags)])
        assert(result.returncode == 0)
        self.late_init_success = True

    def generate(self, counter: int, n: int) -> list[InputJustSeqNum]:
        return iter([InputJustSeqNum(counter, n, self.seq_len, self)])

    def get_real_input(self, inp: InputJustSeqNum) -> InputWithValues:
        assert(self.late_init_success)
        assert(inp.n == 1)
        data = subprocess.run([self.input_generator_path, str(self.seed), str(inp.seq_num), str(self.seq_len)], capture_output=True, text=True, check=True).stdout
        new = InputWithValues.from_str(data)
        new.seq_len = inp.seq_len
        return new

    def get_build_flags(self) -> tuple[set, set]:
        flags, non_repro_flags = super().get_build_flags()
        non_repro_flags |= {f"-DMAX_SEQ_LEN={self.seq_len}", "-DJUST_SEQ_NUM"}
        return flags, non_repro_flags

###############################################################################

if __name__ == '__main__':
    import argparse

    from pyutils.shared_logic import parse_and_set_flags
    parse_and_set_flags()

    from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_common_argparser, parse_flags

    parser = get_common_argparser()
    parser_add_common_extension_parsing(parser)
    OfflineRandomDiffFuzzGenerator.parser_add_args(parser)

    args = parser.parse_args()
    generator = OfflineRandomDiffFuzzGenerator.from_args(args)
    collection = generator.instruction_collection

    flags, non_repro_flags = parse_flags(args)
    generator_flags, generator_non_repro_flags = generator.get_build_flags()
    flags |= generator_flags
    non_repro_flags |= generator_non_repro_flags

    generator.early_init(flags.union(non_repro_flags))
    generator.late_init(flags.union(non_repro_flags))

    print(next(generator.generate(0, 1)).to_input_with_values().to_yaml(instruction_collection=collection))
