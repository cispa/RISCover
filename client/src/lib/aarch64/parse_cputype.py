#!/usr/bin/env python3
import argparse
import os
import re
import sys
import yaml

from aarch64_microarch_db_lib import load_db, dump_db

def prettify_part_macro(prefix: str, macro_tail: str) -> str:
    # Example: prefix=ARM, tail=CORTEX_A53 -> Cortex-A53
    name = re.sub(r"^.*?_PART_", "", f"{prefix}_CPU_PART_{macro_tail}")
    tokens = [t for t in name.split("_") if t]
    return "-".join(tokens).title().replace("-A", "-A")


# Pretty vendor mapping by implementer macro suffix
pretty_vendor = {
    'ARM': 'Arm', 'BRCM': 'Broadcom', 'CAVIUM': 'Cavium', 'FUJITSU': 'Fujitsu',
    'HISI': 'HiSilicon', 'APM': 'APM', 'QCOM': 'Qualcomm',
    'APPLE': 'Apple', 'MICROSOFT': 'Microsoft', 'AMPERE': 'Ampere',
}

def parse_cputype_header(path: str):
    implementers_by_name = {}
    parts = []  # list of (prefix, part_val, part_name)
    imp_re = re.compile(r"^\s*#\s*define\s+[A-Za-z0-9]+_CPU_IMP_([A-Za-z0-9_]+)\s+0x([0-9A-Fa-f]+)\b")
    part_re = re.compile(r"^\s*#\s*define\s+([A-Za-z0-9]+)_CPU_PART_([A-Za-z0-9_]+)\s+0x([0-9A-Fa-f]+)\b")
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = imp_re.match(line)
            if m:
                name, hexval = m.groups()

                val = int(hexval, 16)
                assert val <= 0xff
                pretty_name = pretty_vendor.get(name, name.title())

                implementers_by_name[name] = (pretty_name, val)
                continue

    db = {}
    for name, vid in implementers_by_name.values():
        db[vid] = {}
        db[vid]["vendor"] = name
        db[vid]["parts"] = {}

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = part_re.match(line)
            if m:
                prefix, macro_tail, hexval = m.groups()
                val = int(hexval, 16)
                assert val <= 0xfff
                vid = implementers_by_name[prefix][1]
                db[vid]["parts"][val] = prettify_part_macro(prefix, macro_tail)

    return db


def update_db(db, new_db):
    for vid, vendor_data in new_db.items():
        if not vid in db:
            db[vid] = {}
            db[vid]["vendor"] = vendor_data["vendor"]
            db[vid]["parts"] = {}
        db_vendor_data = db[vid]
        for pid, pname in vendor_data["parts"].items():
            db_vendor_data["parts"][pid] = pname

def main():
    parser = argparse.ArgumentParser(description="Parse cputype.h and update aarch64_microarch_db.json")
    parser.add_argument("path", help="Path to arch/arm64/include/asm/cputype.h")
    args = parser.parse_args()

    db = load_db()

    new_db = parse_cputype_header(args.path)
    update_db(db, new_db)

    db_path = dump_db(db)
    print(f"Updated {db_path}")


if __name__ == "__main__":
    main()
