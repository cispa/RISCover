#!/usr/bin/env python3
import argparse
from pyutils.generation.generators.templategenerator import TemplateDifffuzzGenerator
from pyutils.generation.generators.randomdifffuzzgenerator import RandomDiffFuzzGenerator
from pyutils.generation.generators.offlinerandomdifffuzzgenerator import OfflineRandomDiffFuzzGenerator

all_generators = {
    "TemplateDifffuzzGenerator": TemplateDifffuzzGenerator,
    "RandomDiffFuzzGenerator": RandomDiffFuzzGenerator,
    "OfflineRandomDiffFuzzGenerator": OfflineRandomDiffFuzzGenerator,
}

def parser_add_generator_flags(parser):
    tmp_pa = argparse.ArgumentParser(add_help=False)
    tmp_pa.add_argument('--generator', choices=all_generators.keys(), default='OfflineRandomDiffFuzzGenerator')
    parser.add_argument('--generator', choices=all_generators.keys(), default='OfflineRandomDiffFuzzGenerator')

    _args, _ = tmp_pa.parse_known_args()
    generator = all_generators[_args.generator]

    generator.parser_add_args(parser)

def get_generator_from_args(args):
    generator = all_generators[args.generator]
    return generator.from_args(args)
