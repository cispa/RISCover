#!/usr/bin/env python3
import argparse
import signal
import tabulate
import sys

# TODO: fix pseudo also hinting at extensions (e.g. zihintntl)
# TODO: any way to remove false positives like clrov, rdov (they are  there because of pseudo on csr)

from pyutils.generation import gen_known_instrs, pack_batches, gen_new_batches, pack_instrs, RvOpcodesDis
from pyutils.util import Header, parse_network_block, gp, fp, others, sig_to_str, sig_to_color_str, sig_to_color_str_tex, color_str, dump_json, client_to_header
from pyutils.server import start_server

# TODO: currently needs to be run with 1 instance

# TODO: scan for unratified
# 'rv*', 'unratified/rv*'
# NOTE: we need to scan for the exact same here as undocfuzz, otherwise we miss, maybe move to own file

# TODO: vlb_v gives sigill. Why?
# ./start_clients.sh --detach --instances 1 lab42 all -- server-difffuzz/docfuzz.py --extensions custom/rv_v_0.7.1

# ./start_clients.sh --instances 1 lab42 lab46 lab50 -- server-difffuzz/docfuzz.py --vector --extensions custom/rv_v_0.7.1

max_batches_size = 10000

data=[]
data_diffs = []

diffcount = 0
diffsicount = 0

extensions_per_machine = {}

client_info = None
first_addi = True
def analysis(batch, results, client_info, num, meta):
    global diffs, diffcount, diffsicount, first_addi, data, data_diffs, rvop, client_info

    if not client_info:
        client_info=client_info

    instr = meta["dis"]

    signum = None
    diff = False
    diffsi = False
    # results["lab28"].signum = signal.SIGILL
    for client, res in results.items():
        if signum != None:
            if res.signum != signum:
                diff = True
        else:
            signum = res.signum
        if res.signum == signal.SIGILL and (res.si_code != 0 and res.si_code != 1):
            diffsi = True
    if diff or diffsi:
        diffcount += 1
        if diffsi:
            diffsicount += 1

    # TODO: we use only the first one here, since that is the main extension
    # we would need to find a way to tell if an extension is only partially supported, just because its a subset of another one
    extensions = rvop.insns[instr]["extension"][0:1]

    row = {}
    row["instr"] = batch
    row["mnemonic"] = instr
    row["extensions"] = extensions
    row["results"] = {}
    # TODO: should we use something else? rich?
    # if signum == signal.SIGILL or diff or True:
    # if diff:
    #     a=[color_str(b) for b in a]
    for client, res in results.items():
        row["results"][client] = {}
        row["results"][client] = {
            "signum": res.signum,
            "si_code": res.si_code,
        }

    if diff:
        row["diff"] = True
        data_diffs += [row]
    else:
        row["diff"] = False

    data += [row]

    for ext in extensions:
        for client, res in results.items():
            if res.signum != signal.SIGILL:
                if client not in extensions_per_machine:
                    extensions_per_machine[client]={}
                if ext in extensions_per_machine[client]:
                    extensions_per_machine[client][ext].add(instr)
                else:
                    extensions_per_machine[client][ext] = set([instr])

first = True
def generate(counter, verbose):
    global first, rvop
    if first:
        first = False
        # batches = pad_instrs(gen_known_instrs()+[0x21080d7], batches_of)
        batches, dis = gen_known_instrs(rvop)
        meta = [{"dis": d} for d in dis]
        packed = pack_instrs(batches)
        return batches, packed, (None, meta)
    else:
        return None, None, None

def print_diff_table(data):
    global client_info

    header=["Instr", "Mnemonic", "Extensions"]
    for client in data[0]["results"]:
        header += [client_to_header(client, client_info)]
    table = [header]
    for res in data:
        row = [hex(res["instr"]), res["mnemonic"], res["extensions"]]
        for client, r in res["results"].items():
            row += [sig_to_color_str(r["signum"])+f"({r['si_code']})"]
        if res["diff"]:
            row+=[color_str("xxxxxxxxxxxxxxxxxxxxx")]
        table += [row]
        if len(table) % 30 == 0:
            table += [header]

    print(tabulate.tabulate(table, tablefmt='plain'))

def main():
    global args, data, data_diffs, diffcount, diffsicount, rvop

    ###################### Shared Logic Part ################################

    from pyutils.shared_logic import get_common_argparser, parse_args, parse_flags, print_infos_if_needed

    parser = get_common_argparser()

    parser.add_argument('--until', type=int)
    parser.add_argument('--extensions', nargs='+')

    args = parse_args(parser)
    flags, non_repro_flags = parse_flags(args)

    flags |= non_repro_flags
    flags.add("-DMAX_SEQ_LEN=1")

    print_infos_if_needed(args, flags.union(non_repro_flags))

    ##########################################################################

    if args.extensions:
        rvop = RvOpcodesDis(args.extensions)
    else:
        rvop = RvOpcodesDis()

    start_server(args, max_batches_size, generate, analysis)

    print_diff_table(data)
    print("---------------------")
    if len(data_diffs) > 0:
        print_diff_table(data_diffs)
        print("---------------------")
    print(f"There were {diffcount} diffs")
    print(f"There were {diffsicount} si_code diffs")

    print()
    print("Extensions per machine:")
    for machine, a in extensions_per_machine.items():
        print(f"{client_to_header(machine, client_info)}:")
        extensions = []
        partially = []
        for ext, instr_set in a.items():
            if len(instr_set) == len(rvop.instr_per_extension[ext]):
                extensions += [ext]
            elif len(instr_set) != 0:
                not_implemented = []
                implemented = []
                for instr in rvop.instr_per_extension[ext]:
                    if instr not in instr_set:
                        not_implemented+=[instr]
                    else:
                        implemented+=[instr]
                # extensions += [ext]
                partially += [(ext, f"{len(instr_set)}/{len(rvop.instr_per_extension[ext])}", not_implemented, implemented)]
        print(", ".join(sorted(extensions)))
        part = []
        for (ext, a, missing, implemented) in sorted(partially):
            part += [[ext, str(a), "missing", ', '.join(missing)], [None, None, "implemented", ', '.join(implemented)]]
        if len(part) > 0:
            print("partially:")
            print(tabulate.tabulate(part, tablefmt='plain'))
        print()

    dump_json(data, "docfuzz-results.json")
    dump_json(data_diffs, "docfuzz-results-diffs.json")

if __name__ == '__main__':
    main()
