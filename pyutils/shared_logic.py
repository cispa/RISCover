#!/usr/bin/env python3
import argparse
import yaml
import pyutils.config as config
import glob
import os
import sys

from pyutils.util import option_set

flags_parsed = False
def get_most_common_argparser():
    global flags_parsed
    # If this fails, call parse_and_set_flags first before importing anything else.
    # Check the other cli clients. E.g. diffuzz-server.py
    assert(flags_parsed)
    return argparse.ArgumentParser()

# So that these flags are set before any other code runs
def parse_and_set_flags():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--meta', action='store_true')
    parser.add_argument('--no-check-mem', action='store_true')
    parser.add_argument('--no-auto-map-mem', action='store_true')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--vector', action='store_true')
    parser.add_argument('--floats', action='store_true')
    _args, _ = parser.parse_known_args()

    if _args.meta:
        config.META = True
    if _args.no_check_mem:
        config.CHECK_MEM = False
    if _args.no_auto_map_mem:
        config.AUTO_MAP_MEM = False
    if _args.verbose:
        config.VERBOSE = True
    if _args.vector:
        config.VECTOR = True
    if _args.floats:
        config.FLOATS = True

    match config.ARCH:
        case "riscv64":
            # Vector and floating extension need to be combined on riscv64
            # since they don't share registers (this is a limitation of the client code)
            # NOTE: this is an assertion and not a print because that is easier visible through the start_clients.sh wrapper
            assert(not config.VECTOR or config.FLOATS)
        case "aarch64":
            # Vector and floating extension are exclusive on aarch64
            # NOTE: this is an assertion and not a print because that is easier visible through the start_clients.sh wrapper
            assert(not (config.VECTOR and config.FLOATS))

    global flags_parsed
    flags_parsed = True

def parser_add_common_extension_parsing(parser):
    global all_extensions
    match config.ARCH:
        case "riscv64":
            from pyutils.riscv.riscv_instruction_collection import RiscvInstructionCollection
            extensions = [RiscvInstructionCollection.ALL_EXTENSION_GLOBS[0]]
            all_extensions = RiscvInstructionCollection.ALL_EXTENSION_GLOBS
            remove_extensions = []
            if not config.FLOATS:
                remove_extensions += ["**/*zfa*", "**/*zfh*", "**/*_f*", "**/*_d*", "**/*_q*"]
            if not config.VECTOR:
                remove_extensions += ["rv_v*"]
        case "aarch64":
            extensions = [ "base", "sve", "sme", "fpsimd" ]
            all_extensions = [ "base", "sve", "sme", "fpsimd" ]
            remove_extensions = []
    parser.add_argument('--extensions', nargs='+', default=extensions, help="Supports globs. E.g. rv64*")
    parser.add_argument('--all-extensions', action='store_true', help="Enable all known extensions.")
    parser.add_argument('--add-extensions', nargs='+', default=[], help="Add these extensions to the predefined list of extensions.")
    parser.add_argument('--remove-extensions', nargs='+', default=remove_extensions, help="Which extensions should be excluded after expanding included extensions. By default excludes e.g. the vector extension on riscv64 when --vector is not used.")
    parser.add_argument('--add-remove-extensions', nargs='+', default=[], help="Add these extensions to the remove list.")
    parser.add_argument('--instruction-db', default='instructions.json', help="Use this filename as instruction database. Useful for switching to instructions_rv_v_0.7.1.json.")

def get_common_argparser():
    parser = get_most_common_argparser()
    parser.add_argument('--verbose', action='store_true')

    parser.add_argument('--expect', type=int, help='Wait for n clients, then just start')
    parser.add_argument('--port', type=int, default=1337)

    parser.add_argument('--group-by', choices=['midr', "one-per-midr", 'hostname+microarch', 'hostname', 'none'], default='midr')

    # TODO(now): maybe do a flag build client instead
    parser.add_argument('--print-flags', action='store_true')
    parser.add_argument('--print-files', action='store_true')

    parser.add_argument('--no-check-mem', action='store_true')
    parser.add_argument('--no-auto-map-mem', action='store_true')
    parser.add_argument('--vector', action='store_true')
    parser.add_argument('--floats', action='store_true')
    parser.add_argument('--meta', action='store_true')
    parser.add_argument('--single-client', action='store_true')

    return parser

def get_collection_from_args(args):
    if args.all_extensions:
        global all_extensions
        extensions = all_extensions
    else:
        extensions = args.extensions
    extensions += args.add_extensions
    # Remove exact matching globs here to make the print right after somehow coherent
    # its not perfect because single extensions can be removed again by this arg here
    # Then later remove the expanded form
    remove_extensions = args.remove_extensions+args.add_remove_extensions
    for rm in remove_extensions:
        if rm in extensions:
            extensions.remove(rm)
                                           # ugly fix so that this is not printed for --print-flags
    print("Using extensions:", extensions, file=sys.stderr)
    match config.ARCH:
        case "riscv64":
            from pyutils.riscv.riscv_instruction_collection import RiscvInstructionCollection
            extensions = RiscvInstructionCollection.get_extensions_matching_globs(extensions, db=args.instruction_db)
            rm_extensions = RiscvInstructionCollection.get_extensions_matching_globs(remove_extensions, db=args.instruction_db)
        case "aarch64":
            from pyutils.arm.arm_instruction_collection import ArmInstructionCollection
            extensions = extensions
            rm_extensions = remove_extensions
    for rm in rm_extensions:
        if rm in extensions:
            extensions.remove(rm)
                                   # ugly fix so that this is not printed for --print-flags
    print("Expanded:", extensions, file=sys.stderr)
    match config.ARCH:
        case "riscv64":
            return RiscvInstructionCollection(extensions=extensions, db=args.instruction_db)
        case "aarch64":
            return ArmInstructionCollection(extensions=extensions, db=args.instruction_db)

def parse_args(parser):
    args = parser.parse_args()
    return args

def load_config_from_repro(path):
    with open(path, 'r') as f:
        repro = yaml.safe_load(f)

        assert(config.ARCH == repro["arch"]);

        flags = set(repro["flags"])

        config.CHECK_MEM = not "-DCHECK_MEM" in flags
        config.AUTO_MAP_MEM = not "-DAUTO_MAP_MEM" in flags
        config.VERBOSE = "-DVERBOSE" in flags
        config.VECTOR = "-DVECTOR" in flags
        config.FLOATS = "-DFLOATS" in flags

def parse_flags(args) -> tuple[set, set]:
    flags = set()
    non_repro_flags = set()
    if option_set(args, "single_client"):
        non_repro_flags.add("-DSINGLE_THREAD")

    if config.META:
        flags.add("-DMETA")
    if config.CHECK_MEM:
        flags.add("-DCHECK_MEM")
    if config.AUTO_MAP_MEM:
        flags.add("-DAUTO_MAP_MEM")
    if config.VERBOSE:
        non_repro_flags.add("-DVERBOSE")
    if config.VECTOR:
        flags.add("-DVECTOR")
    if config.FLOATS:
        flags.add("-DFLOATS")

    return flags, non_repro_flags

def print_infos_if_needed(args, flags, files=[]):
    if args.print_flags:
        print(" ".join(flags))
        exit(0)
    if args.print_files:
        print(" ".join(files))
        exit(0)
