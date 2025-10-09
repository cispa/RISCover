#!/usr/bin/env python3

from pyutils.util import Header, parse_network_block, gp, fp, others, sig_to_str, sig_to_color_str, sig_to_color_str_tex, color_str, dump_json, load_json, make_latex_safe, client_to_header
from texttable import Texttable
import latextable

def latex_table(data, caption):

    out = ""

    table = []
    header=["Instr", "Mnemonic", "Extensions"]

    for client in data[0]["results"]:
        header += [client_to_header(client)]

    for res in data:
        # TODO: add all extensions
        row = ["\\texttt{"+hex(res["instr"])+"}", make_latex_safe(res["mnemonic"]), ", ".join(make_latex_safe(e) for e in [res["extensions"][0]])]
        for client, r in res["results"].items():
            row += [sig_to_color_str_tex(r["signum"])]
        table += [row]

    size = 35
    for i in range(0, len(table), size):
        sli = table[i:i+size]

        table_1 = Texttable()
        # table_1.set_cols_align(["l", "r", "c"])
        # table_1.set_cols_valign(["t", "m", "b"])
        table_1.add_rows([header]+sli)

        # out += latextable.draw_latex(table_1, caption=caption, label="table:example_table")
        out += latextable.draw_latex(table_1, caption=caption)
        # out += latextable.draw_latex(table_1)

        out += "\n"

    return out

with open("article/docfuzz-results.tex", "w") as f:
    data = load_json("docfuzz-results.json")
    f.write(latex_table(data, "Full results"))

with open("article/docfuzz-results-diffs.tex", "w") as f:
    data = load_json("docfuzz-results-diffs.json")
    f.write(latex_table(data, "Diffs"))
