#!/usr/bin/env python3
import os

# Use the shared DB helpers
from aarch64_microarch_db_lib import load_db, dump_db  # dump_db imported per request, not used here


def to_int(v, bits=None):
    """
    Parse an int that may be provided as an int or a (hex) string like '0x41'.
    Optionally mask to the given bit-width.
    """
    if isinstance(v, int):
        out = v
    elif isinstance(v, str):
        out = int(v, 0)
    else:
        raise ValueError(f"Unsupported number type: {type(v)}")
    if bits is not None:
        out &= (1 << bits) - 1
    return out


def main():
    # Hardcoded output path relative to this script
    here = os.path.dirname(os.path.abspath(__file__))
    out_header = os.path.join(here, 'generated_midr_db.h')

    db = load_db()

    # Normalize and collect
    normalized = []
    vendors = {}

    for impl_hex, group in (db or {}).items():
        impl = to_int(impl_hex, bits=8)
        vendor = (group.get('vendor') or '')
        vendors[impl] = vendor

        parts = group.get('parts') or {}
        if not isinstance(parts, dict):
            raise ValueError(f"'parts' for implementer {impl_hex} must be a mapping of part_id->name")

        for part_hex, name in parts.items():
            part = to_int(part_hex, bits=12)
            normalized.append((impl, part, name or ''))

    # Sort for stable header
    normalized.sort()

    with open(out_header, 'w', encoding='utf-8') as f:
        f.write("// Generated from aarch64_microarch_db.yaml by microarch_db_genheader.py\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("typedef struct { uint16_t implementer; uint16_t part; const char *name; } midr_name_entry;\n")
        f.write("static const midr_name_entry midr_name_table[] = {\n")
        for impl, part, name in normalized:
            safe = (name or '').replace('"', '\\"')
            f.write(f"    {{ 0x{impl:02x}, 0x{part:03x}, \"{safe}\" }},\n")
        f.write("};\n")
        f.write("static const unsigned midr_name_table_len = sizeof(midr_name_table)/sizeof(midr_name_table[0]);\n\n")

        f.write("typedef struct { uint16_t implementer; const char *vendor; } midr_vendor_entry;\n")
        f.write("static const midr_vendor_entry midr_vendor_table[] = {\n")
        for impl in sorted(vendors.keys()):
            vname = (vendors[impl] or '').replace('"', '\\"')
            f.write(f"    {{ 0x{impl:02x}, \"{vname}\" }},\n")
        f.write("};\n")
        f.write("static const unsigned midr_vendor_table_len = sizeof(midr_vendor_table)/sizeof(midr_vendor_table[0]);\n")

    print(f"Wrote {out_header} with {len(normalized)} name entries and {len(vendors)} vendor entries")


if __name__ == '__main__':
    main()
