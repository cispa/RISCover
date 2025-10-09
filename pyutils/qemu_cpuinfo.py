#!/usr/bin/env python3

from pyutils.cpuinfo import CPUInfo
import pyutils.config as config

class QemuCPUInfo(CPUInfo):
    def __init__(self, qemu_string):
        super().__init__()

        # Custom implementer
        self.implementer = 0x7F

        match config.ARCH:
            case "riscv64":
                self.architecture = 0xf
            case "aarch64":
                # ARMv8
                self.architecture = 0x8

        self.variant = 0
        self.part = 0
        self.revision = 0

        assert(qemu_string.startswith("qemu"))

        version_str = qemu_string.removeprefix("qemu-")
        if version_str == "latest":
            self.variant = 0xF
            self.part = 0xFFF
            self.revision = 0xF
        else:
            # Example: 'qemu-v8.1' -> variant=8, revision=1
            # Extract version parts
            version_parts = version_str.lstrip("v").split(".")
            try:
                self.variant = int(version_parts[0]) & 0xF
            except (IndexError, ValueError):
                self.variant = 0
            try:
                self.revision = int(version_parts[1]) & 0xF
            except (IndexError, ValueError):
                self.revision = 0
