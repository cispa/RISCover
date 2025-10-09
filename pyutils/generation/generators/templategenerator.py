#!/usr/bin/env python3

# Copy over this template to a new file, rename the generator and add it to allgenerators.py.
# You can then call it either directly (see main at the end of this file) or via diffuzz-server.py.
# For that specify --generator <your-generator-name>. All notes on diffuzz-server.py apply.

# Just for quick testing. Check the bottom of this file.
if __name__ == '__main__':
    import sys, os
    sys.path.append(os.getenv("FRAMEWORK_ROOT"))

###############################################################################
###############################################################################

from typing import Iterator

from pyutils.generation.generators.randomdifffuzzgenerator import DiffFuzzGenerator
from pyutils.assembly import asm_opcodes, asm_keystone
from pyutils.inp import InputWithSparseValues, Input, InputWithRegSelect, InputWithValues
from pyutils.util import gp, fp
from pyutils.shared_logic import get_collection_from_args
from pyutils.generation.generationutil import seed_all, init_random_instructions

# Depending on how we send over the state we need different flags at build-time.
# Ignore for now/see below.
# state_method = "nostate"
# state_method = "regselect"
state_method = "full"

def generate(generator, counter, seed) -> Iterator[Input]:
    seed_all(seed)

    seq = []

    # Assemble instructions with keystone
    seq += asm_keystone(f"mov x1, #{generator.example_arg}\nmov x2, #2")

    # Assemble an instruction with libopcodes/gas/pwntools
    seq += asm_opcodes("add x3, x3, #3")

    # We can just load from memory when running without --no-check-mem.
    # The runner will map a page there. First all zeros, then all ones.
    # Then all changed bytes are logged.
    seq += asm_keystone("mov x5, #0xffff0\nldr x6, [x5]")

    # We can also write to memory.
    seq += asm_keystone("str x7, [x5]")

    # We can use the MRA (machine-readable architecture) from ARM to initialize any documented instructions.
    #
    # Picking them randomly and initializing them randomly:
    #                                                                                         Which registers are candidates for random selection.
    #                                                                                         This means registers x0-x4 are used in 90% of cases.
    seq += (i.pack() for i in init_random_instructions(generator.instruction_collection, n=1, num_regs=5))
    # or randomly initialize a specific instruction:
    seq += [generator.instruction_collection.randomly_init_instr(mnemonic="A64.dpimm.addsub_imm.ADD_32_addsub_imm", num_regs=5).pack()]

    # We can also change the encoding of instructions. For example this is mov x1, #7:
    mov = generator.instruction_collection.init_instr(mnemonic="A64.dpimm.movewide.MOVZ_32_movewide")
    mov.hw    = 0
    mov.imm16 = 7
    mov.Rd    = 1
    # Set 64-bit variant.
    mov.sf    = 1
    seq += [mov.pack()]

    # We can also interact with the instruction:
    # print(hex(mov.imm16))
    # mov.pretty_print_instr()
    # print(mov)

    # TODO: also load instructions

    # For a list of available instructions and fields just print them:
    # print("\n".join(generator.instruction_collection.instructions.keys()))
    # print(generator.instruction_collection.instructions["A64.dpimm.movewide.MOVZ_32_movewide"])

    # This is an easy way to make sure that there is a diff between all clients.
    # Use for verifying that the differential fuzzing works.
    # TODO: this does not work because of alarm
    seq += asm_keystone("add x7, x7, #1\nb 0")

    # For register initial state there are currently 3 different options:
    match state_method:
        case "nostate":
            # 1. The registers are initialized to a fix filler value.
            #    No network overhead.
            inp = Input(seq)
        case "regselect":
            # 2. Each register is initialized with a picked value from the fuzzing_value_map (see the fuzzing_value_map.py files).
            #    This is easy on network as each register only requires one byte but only allows for these constants.
            inp = InputWithRegSelect(
                # Select UINT64_MAX for each gp register
                [20]*(len(gp))
                , seq)
        case "full":
            # 3. Pick a value for each register.
            #    Heavy on the network. Depending on if vector regs used, >100B of traffic per input.
            inp = InputWithSparseValues(seq, _gp={
                # The other registers are expanded on the server side to a filler value
                "x3": 42,
            }).to_input_with_values()

    return inp

class TemplateDifffuzzGenerator(DiffFuzzGenerator):
    def __init__(self, instruction_collection, seed, example_arg):
        super().__init__(instruction_collection=instruction_collection)
        self.seed = seed
        self.example_arg = example_arg

    @classmethod
    def from_args(cls, args):
        return cls(instruction_collection=get_collection_from_args(args), seed=args.seed, example_arg=args.example_arg)

    def generate(self, counter: int, n: int) -> Iterator[Input]:
        # We call the local method here just for readability. See above.
        return (generate(self, counter+i, (counter+i)^self.seed) for i in range(n))

    def to_dict(self) -> dict:
        d = super().to_dict()
        return d | {
            "example_arg": self.example_arg,
            "seed": self.seed,
        }

    def parser_add_args(parser):
        parser.add_argument('--example-arg', type=int, default=42)
        parser.add_argument('--seed', type=int, default=0)

    def get_build_flags(self) -> tuple[set, set]:
        flags, non_repro_flags = super().get_build_flags()
        non_repro_flags |= {"-DMAX_SEQ_LEN=20"}

        match state_method:
            case "nostate":
                pass
            case "regselect":
                non_repro_flags |= {"-DWITH_REGS"}
            case "full":
                non_repro_flags |= {"-DWITH_FULL_REGS"}

        return flags, non_repro_flags

###############################################################################
###############################################################################

# Just for quick testing.
if __name__ == '__main__':
    from pyutils.arm.generation import ArmInstructionCollection

    collection = ArmInstructionCollection(extensions=["base", "sve", "sme", "fpsimd"])
    generator = TemplateDifffuzzGenerator(instruction_collection=collection, seed=0, example_arg=3)
    print(next(generator.generate(0, 1)).to_input_with_values().to_yaml(instruction_collection=collection))
