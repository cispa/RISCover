#!/usr/bin/env python3
import sys, os
sys.path.append(os.getenv("FRAMEWORK_ROOT"))

import argparse

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

from pyutils.shared_logic import parser_add_common_extension_parsing, get_collection_from_args, get_most_common_argparser
from pyutils.generation.instruction import Instruction

parser = get_most_common_argparser()
parser_add_common_extension_parsing(parser)
parser.add_argument('file')
args = parser.parse_args()
collection = get_collection_from_args(args)

with open(args.file, "r") as f:
    b = set()
    for l in f.readlines():
        i = int(l.split(",")[0], 16)

        mnemonic = collection.disassemble(i)
        if not mnemonic:
            if i in b:
                continue
            print(hex(i))
            b.add(i)

print(b)
print(len(b))
