#!/usr/bin/env python3
import re

# /* linux/arch/arm64/kernel/cpuinfo.c */
# /* linux/arch/arm64/include/asm/cputype.h */
MIDR_IMPLEMENTOR_SHIFT  = 24
MIDR_VARIANT_SHIFT      = 20
MIDR_ARCHITECTURE_SHIFT = 16
MIDR_PARTNUM_SHIFT      =  4
MIDR_REVISION_SHIFT     =  0

class CPUInfo:
    def __init__(self):
        pass

    def midr(self):
        midr = 0;
        midr |= self.implementer   << MIDR_IMPLEMENTOR_SHIFT;
        midr |= self.architecture  << MIDR_ARCHITECTURE_SHIFT;
        midr |= self.variant       << MIDR_VARIANT_SHIFT;
        midr |= self.part          << MIDR_PARTNUM_SHIFT;
        midr |= self.revision      << MIDR_REVISION_SHIFT;
        return midr

    def __repr__(self):
        return hex(self.midr())
        # return (f"MIDR            : {hex(self.midr())}\n"
        #         f"implementer     : 0x{self.implementer:02X}\n"
        #         f"architecture    : 0x{self.architecture:X}\n"
        #         f"variant         : 0x{self.variant:X}\n"
        #         f"part            : 0x{self.part:03X}\n"
        #         f"revision        : 0x{self.revision:X}")

def parse_cpu_possible(buf):
    total = 0
    # Split the line by comma.
    for token in buf.split(","):
        token = token.strip()  # Remove any leading/trailing whitespace.
        if '-' in token:
            try:
                start, end = map(int, token.split("-"))
            except ValueError:
                print(f"Error parsing range: {token}")
                sys.exit(1)
            total += (end - start + 1)
        else:
            # Count a single CPU id.
            total += 1

    return total

def parse_cpuinfo(data, sys_possible):
    cpus = []
    report_back_n = 0  # Number of cpus added in the current block
    in_block = False   # Indicates that we are inside a block of CPU info

    for line in data.split("\n"):
        # Blank line indicates the end of a block.
        if line.strip() == "":
            report_back_n = 0
            in_block = False
            continue

        # Check for a "processor" line, e.g. "processor : 0"
        m = re.match(r"processor\s*:\s*(\d+)", line)
        if m:
            processor = int(m.group(1))
            cpu = CPUInfo()
            cpu.architecture = 8
            cpu.processor = processor
            cpus.append(cpu)
            report_back_n += 1
            in_block = True
            continue

        # When in a block, check for the other keys.
        if in_block:
            # A mapping from key strings in /proc/cpuinfo to attribute names.
            key_attr_map = {
                "CPU implementer": "implementer",
                "CPU architecture": "architecture",
                "CPU variant": "variant",
                "CPU part": "part",
                "CPU revision": "revision"
            }
            for key, attr in key_attr_map.items():
                # Check if the line starts with the key.
                if line.startswith(key):
                    # Expecting a format like: "CPU implementer : 0x41" or a decimal number.
                    parts = line.split(":", 1)
                    if len(parts) < 2:
                        continue
                    # Use int with base 0 to handle numbers in hex (e.g., 0x41) or decimal.
                    try:
                        value = int(parts[1].strip(), 0)
                    except ValueError:
                        continue
                    # Update the attribute for each CPU in the current block.
                    for cpu in cpus[-report_back_n:]:
                        setattr(cpu, attr, value)


            #     if "Features" in key:
            #         current_entry.features = list(filter(lambda a: a, [k.strip() for k in value.split(" ")]))
            # # TODO: else store the data that follows
            # # CPU info	: 2b0c1000012e0f000015333657334150
            # # Serial		: 30ebbc9b-92f7-4647-b1db-001e064b65ad
            # # Hardware	: Hardkernel ODROID-C4
            # # Revision	: 0500

    assert(len(cpus) == parse_cpu_possible(sys_possible))

    return cpus

if __name__ == "__main__":
    import sys
    with open(sys.argv[1], 'r') as file:
        content = file.read()
    with open(sys.argv[2], 'r') as file:
        content2 = file.read()
    parsed_data = parse_cpuinfo(content, content2)
    print(parsed_data)
    # print(parsed_data[0].features)
    print(list(hex(j) for j in set(i.midr() for i in parsed_data)))
