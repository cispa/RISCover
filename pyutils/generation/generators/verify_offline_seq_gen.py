#!/usr/bin/env python3
import sys, os
sys.path.append(os.getenv("FRAMEWORK_ROOT"))

import argparse
from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_most_common_argparser
from pyutils.generation.rng import SharedRng
from pyutils.generation.generators.genheader import write_header

parser = get_most_common_argparser()
parser_add_common_extension_parsing(parser)
args = parser.parse_args()

collection = get_collection_from_args(args)
write_header(collection)

rng = SharedRng(50)

for _ in range(5):
    m = rng.custom_choices(list(collection.instructions.keys()), k=1)[0]
    i = collection.randomly_init_instr(mnemonic=m, num_regs=5).pack()
    print(m, hex(i))
