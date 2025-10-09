#!/usr/bin/env python3
import os
import yaml

here = os.path.dirname(os.path.abspath(__file__))
db_path = os.path.join(here, 'aarch64_microarch_db.yaml')

def hexint_presenter(dumper, data):
    return dumper.represent_int(hex(data))
yaml.add_representer(int, hexint_presenter)

def dump_db(db):
    with open(db_path, 'w', encoding='utf-8') as f:
        yaml.dump(db, f, sort_keys=False)

    return db_path

def load_db():
    if os.path.exists(db_path):
        with open(db_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
    else:
        return {}
