#!/usr/bin/env python3
import yaml
import shutil
from pathlib import Path
import sys

from pyutils.shared_logic import parse_and_set_flags
parse_and_set_flags()

from pyutils.repro import Repro
from pyutils.result import LenientResult
from pyutils.difffuzzserver import output_dir

from pyutils.shared_logic import load_config_from_repro

# TODO: write a script that prints just the comments (regenerate them) of repros together with file path

# NOTE: checkout one reproducer per class like this
# rm -rf reproducers/classes && python server/cluster.py
# for dir in ~/difffuzz-framework/results/reproducers/classes/*/; do find "$dir" -maxdepth 1 -type f | head -n 1; done | xargs bat --style=header --line-range :40

def filter_diffs(diffs):
    return diffs.difference({"pstate", "si_addr", "si_pc", "si_code"})

def main():
    real_classes = 0
    classes = {}

    if len(sys.argv) < 2:
        results = output_dir
    else:
        results = sys.argv[1]

    directory = Path(results+"/reproducers")
    files = list(filter(lambda f: f.is_file() and f.suffix.lower() in ['.yaml', '.yml'], directory.iterdir()))

    # TODO: assert later that other repros have the same flags?
    load_config_from_repro(files[0])

    # TODO: multi process?
    processed_files = 0
    last_printed_perc = None
    for path in files:
        with path.open('r') as f:
            d = yaml.safe_load(f)
            # TODO: remove this later when we have better termination
            if not d or not "flags" in d:
                print(f"{path} is broken. This is expected since non-obstructive termination of the server is not implemented yet.")
                continue
            repro = Repro.from_dict(d)

            new_result_to_clients = {LenientResult(res): clients for res, clients in repro.result_to_clients.items()}
            repro.result_to_clients = new_result_to_clients

            # If the reproducer is now filtered out, e.g., because of only si_addr diff
            if not filter_diffs(repro.all_diffs()):
                new_class = "filtered-out"
            else:
                existing_class = None
                for cls, class_repro in classes.items():
                    if repro.similar(class_repro):
                        existing_class = cls
                        break

                if not existing_class:
                    real_classes += 1
                    new_class = f"class-{real_classes:03}"
                else:
                    new_class = existing_class

            if new_class not in classes:
                classes[new_class] = repro
                class_dir = directory / "classes" / new_class
                class_dir.mkdir(parents=True)
                print(f"New class: {class_dir}")
            else:
                class_dir = directory / "classes" / new_class

            new_repro_path = class_dir / path.name
            shutil.copy(path, new_repro_path)

        processed_files += 1
        perc = (processed_files * 100) // len(files)
        if last_printed_perc != perc:
            print(f"Progress: {perc}%")
            last_printed_perc = perc

    print(f"Total classes: {len(classes)}")

if __name__ == '__main__':
    main()
