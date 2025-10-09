#!/usr/bin/env python3
from os import getenv

ARCH = getenv("ARCH")
architectures = ["aarch64", "riscv64"]
if not ARCH:
    print(f"Make sure ARCH env var is set. Options: {architectures}")
    exit(1)
if not ARCH in architectures:
    print(f"Set ARCH env var to one of the valid options: {architectures}")
    exit(1)

FRAMEWORK_ROOT = getenv("FRAMEWORK_ROOT")
if not FRAMEWORK_ROOT:
    print(f"Make sure FRAMEWORK_ROOT env var is set.")
    exit(1)

META = False
CHECK_MEM = True
AUTO_MAP_MEM = True
VERBOSE = False
VECTOR = False
FLOATS = False
