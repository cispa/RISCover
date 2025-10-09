#!/usr/bin/env python3
import struct
import signal
import tabulate
from datetime import datetime
import json
import os
import shutil
import random
import numpy as np
from itertools import chain, combinations
import yaml
import resource

import pyutils.config as config
from pyutils.lscpu import CPUInfo, Microarchitecture

tabulate.PRESERVE_WHITESPACE = True

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    RED = '\033[31m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

match config.ARCH:
    case "riscv64":
        gp = [
            "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0",
            "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
            "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
            "s11", "t3", "t4", "t5", "t6"
        ]
        reg_num_to_name_gp = ["x0"]+gp
        fp_others = [
            "fcsr",
        ]
        fp = [
            "ft0", "ft1", "ft2", "ft3",
            "ft4", "ft5", "ft6", "ft7", "fs0", "fs1", "fa0", "fa1", "fa2",
            "fa3", "fa4", "fa5", "fa6", "fa7", "fs2", "fs3", "fs4", "fs5",
            "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9",
            "ft10", "ft11"
        ]
        reg_num_to_name_fp = fp
        vec = [
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8",
            "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18",
            "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28",
            "v29", "v30", "v31"
        ]

        regs_mapping = list(gp)
        if config.FLOATS:
            regs_mapping += fp_others+fp
        if config.VECTOR:
            regs_mapping += vec

        VEC_REG_SIZE = 16
        PAGE_SIZE = 4096
    case "aarch64":
        gp = [
            "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",
            "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
            "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28",
            "x29", "x30", "sp"
        ]
        gp_others = [
            "pstate"
        ]
        fp_others = [
            "fpsr"
        ]
        fp = [
            "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8",
            "d9", "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18",
            "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27", "d28",
            "d29", "d30", "d31"
        ]
        vec = [
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8",
            "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18",
            "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28",
            "v29", "v30", "v31"
        ]

        VEC_REG_SIZE = 16
        PAGE_SIZE = 16384

        regs_mapping = list(gp+gp_others)
        if config.VECTOR:
            regs_mapping += fp_others+vec
        elif config.FLOATS:
            regs_mapping += fp_others+fp


def read_null_terminated_string(fp):
    string = ""
    while True:
        char = fp.read(1)
        if char == b'\0':
            break
        string += char.decode("ascii")
    return string

def sig_to_str(sig):
    if sig == 0:
        return "OK"
    else:
        return signal.Signals(sig).name

def sig_to_color(sig):
    if sig == 0:
        return bcolors.OKGREEN, "green"
    elif sig == signal.SIGSEGV:
        return bcolors.FAIL, "red"
    elif sig == signal.SIGALRM:
        return bcolors.OKCYAN, "cyan"
    elif sig == signal.SIGTRAP:
        return bcolors.OKCYAN, "cyan"
    else:
        return bcolors.WARNING, "yellow"

def color_str(s, color=bcolors.WARNING):
    return f"{color}{s}{bcolors.ENDC}"

def color_str_tex(s, color="yellow"):
    return "\\textcolor{"+color+"}{"+s+"}"

def load_json(path):
    with open(path, "r") as f:
        return json.loads(f.read())

def dump_json(data, path):
    with open(path, "w") as f:
        f.write(json.dumps(data, indent=4))
    print(f"Wrote {path}")

def make_latex_safe(s):
    return s.replace("_", "\\_")

def client_mic_to_header(client):
    o = ""
    if client.cpu.vendor:
        o += client.cpu.vendor + " "
    return o+f"{client.microarchitecture.model_name}"

def format_vec_sizes(client):
    out=""
    if client.vec_size:
        out=f" VEC={client.vec_size}"
        match config.ARCH:
            case "aarch64":
                if "sve_max_size" in client.other and client.other["sve_max_size"]:
                    out+=f" SVE={client.other["sve_max_size"]}"
                if "sme_max_size" in client.other and client.other["sme_max_size"]:
                    out+=f" SME={client.other["sme_max_size"]}"
    return out

def client_to_header(client):
    return client_mic_to_header(client)+f" ({client.hostname} {hex(client.microarchitecture.midr)}{format_vec_sizes(client)})"

def client_to_header_colored(client):
    return client_mic_to_header(client)+f" ({color_str(client.hostname)} {hex(client.microarchitecture.midr)}{format_vec_sizes(client)})"

def sec_to_str(seconds):
    days = seconds // (24 * 60 * 60)
    seconds %= (24 * 60 * 60)
    hours = seconds // (60 * 60)
    seconds %= (60 * 60)
    minutes = seconds // 60
    seconds %= 60

    if days > 0:
        return f"{days}d, {hours}h"
    elif hours > 0:
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
    else:
        return f"{minutes:02d}:{seconds:02d}"

def sig_to_color_str(sig):
    return color_str(sig_to_str(sig), sig_to_color(sig)[0])

def sig_to_color_str_tex(sig):
    return color_str_tex(sig_to_str(sig), sig_to_color(sig)[1])

# /usr/riscv64-linux-gnu/include/asm-generic/siginfo.h
# NOTE: seem to be the same for aarch64 https://github.com/torvalds/linux/blob/master/include/uapi/asm-generic/siginfo.h
ILL_ILLOPC = 1    # illegal opcode
ILL_ILLOPN = 2    # illegal operand
ILL_ILLADR = 3    # illegal addressing mode
ILL_ILLTRP = 4    # illegal trap
ILL_PRVOPC = 5    # privileged opcode
ILL_PRVREG = 6    # privileged register
ILL_COPROC = 7    # coprocessor error
ILL_BADSTK = 8    # internal stack error
ILL_BADIADDR = 9  # unimplemented instruction address
__ILL_BREAK = 10  # illegal break
__ILL_BNDMOD = 11 # bundle-update (modification) in progress
NSIGILL = 11

def create_results_folder(output_dir):
    if os.path.exists(output_dir):
        alt=output_dir+"-old"
        if os.path.exists(alt):
            shutil.rmtree(alt)
            print(f"Deleted old results dir {alt}")
        shutil.move(output_dir, alt)
        print(f"Moved old results folder to {alt}")
    os.mkdir(output_dir)

def read_undocfuzz_file(path, func):
    with open(path) as f:
        for line in f:
            instr = int(line, 16)
            func(instr)

def read_undocfuzz_file_collect(path):
    with open(path) as f:
        return [int(line, 16) for line in f]

def option_set(args, option):
    return vars(args).get(option, False)

def signal_powerset(l):
    l = set(l)
    return chain.from_iterable([set(c) for c in combinations(l, r)] for r in range(1, len(l)+1))

# Lscpu info for clients where lscpu is broken somehow
c910 = CPUInfo(architecture="riscv64", vendor="T-Head", microarchitectures=[
                Microarchitecture(
                    model_name="XuanTie C910",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    # TODO
                    flags=set(["rv64imafdcsu", "v0p7", "TODO", "xthead"])
                )
            ])
hardcoded_machine_info = {
    "lab32": CPUInfo(architecture="riscv64", vendor="StarFive", microarchitectures=[
                Microarchitecture(
                    model_name="VisionFive U74",
                    num_cores=2,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdc_zicntr_zicsr_zifencei_zihpm
                    flags=set(["rv64imafdc", "zicntr", "zicsr", "zifencei", "zihpm"])
                )
            ]),
    "lab36": CPUInfo(architecture="riscv64", vendor="StarFive", microarchitectures=[
                Microarchitecture(
                    model_name="VisionFive U74",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdc_zicntr_zicsr_zifencei_zihpm_zba_zbb
                    flags=set(["rv64imafdc", "zicntr", "zicsr", "zifencei", "zihpm", "zba", "zbb"])
                )
            ]),
    # TODO: do they have two core types?
    "lab48": CPUInfo(architecture="riscv64", vendor="StarFive", microarchitectures=[
                Microarchitecture(
                    model_name="VisionFive U54",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdc
                    flags=set(["rv64imafdc"])
                )
            ]),
    "lab53": c910,
    "lab46": c910,
    "lab64": c910,
    "lab24": CPUInfo(architecture="riscv64", vendor="T-Head", microarchitectures=[
                Microarchitecture(
                    model_name="XuanTie C906",
                    num_cores=1,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdc_zicntr_zicsr_zifencei_zihpm
                    flags=set(["rv64imafdc", "zicntr", "zicsr", "zifencei", "zihpm", "xthead"])
                )
            ]),
    "lab52": CPUInfo(architecture="riscv64", vendor="T-Head", microarchitectures=[
                Microarchitecture(
                    model_name="XuanTie C908",
                    num_cores=1,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdcvxthead
                    flags=set(["rv64imafdc", "v", "xthead"])
                )
            ]),
    "lab50": CPUInfo(architecture="riscv64", vendor="T-Head", microarchitectures=[
                Microarchitecture(
                    model_name="XuanTie C906",
                    num_cores=1,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdcvu
                    flags=set(["rv64imafdcvu", "xthead"])
                )
            ]),
    "lab77": CPUInfo(architecture="riscv64", vendor="SiFive", microarchitectures=[
                Microarchitecture(
                    model_name="P550",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdch_zicsr_zifencei_zba_zbb_sscofpmf
                    flags=set(["rv64imafdch", "zicsr", "zifencei", "zba", "zbb", "sscofpmf"])
                )
            ]),
    "boom": CPUInfo(architecture="riscv64", vendor="BOOM", microarchitectures=[
                Microarchitecture(
                    model_name="BOOM",
                    num_cores=1,
                    num_sockets=1,
                    threads_per_core=1,
                    # rv64imafdc_zicntr_zicsr_zifencei_zihpm_zca_zcd
                    flags=set(["rv64imafdc", "zicntr", "zicsr", "zifencei", "zihpm", "zca", "zcd"])
                )
            ]),

    #
    # aarch64
    #
    "lab71": CPUInfo(architecture="aarch64", vendor="Huawei", microarchitectures=[
                Microarchitecture(
                    model_name="Kunpeng Pro",
                    # TODO: why is one cpu unavailable? taskset -c 3 ls
                    num_cores=3,
                    num_sockets=1,
                    threads_per_core=1,
                    flags={'fp', 'fphp', 'sha3', 'jscvt', 'sha2', 'sha512', 'sve', 'pmull', 'evtstrm', 'asimdhp', 'cpuid', 'atomics', 'dcpop', 'asimdrdm', 'asimddp', 'asimdfhm', 'asimd', 'ssbs', 'aes', 'sha1', 'crc32', 'sb', 'fcma'}
                )
            ]),
    "lab76": CPUInfo(architecture="aarch64", vendor="Rockchip", microarchitectures=[
                Microarchitecture(
                    model_name="RK3568",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    flags={"fp", "asimd", "evtstrm", "aes", "pmull", "sha1", "sha2", "crc32", "atomics", "fphp", "asimdhp", "cpuid", "asimdrdm", "lrcpc", "dcpop", "asimddp"}
                )
            ]),
    "lab57": CPUInfo(architecture="aarch64", vendor="ARM", microarchitectures=[
                Microarchitecture(
                    model_name="Cortex-A55",
                    num_cores=4,
                    num_sockets=1,
                    threads_per_core=1,
                    flags={"fp", "asimd", "evtstrm", "aes", "pmull", "sha1", "sha2", "crc32", "atomics", "fphp", "asimdhp"}
                )
            ]),
}
serial_to_hostname={
    # Now in HOST var but keep it for later use
    # "71225c9813ab604": "lab76",
    # "001e064b65ad":    "lab57",
}
pixel_9_lscpu = """
Architecture:             aarch64
  CPU op-mode(s):         64-bit
  Byte Order:             Little Endian
CPU(s):                   8
  On-line CPU(s) list:    0-7
Vendor ID:                ARM
  Model name:             Cortex-A520
    Model:                1
    Thread(s) per core:   1
    Core(s) per socket:   4
    Socket(s):            1
    Stepping:             r0p1
    CPU(s) scaling MHz:   68%
    CPU max MHz:          1950.0000
    CPU min MHz:          820.0000
    BogoMIPS:             49.15
    Flags:                fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp sve2 sveaes svepmull
                          svebitperm svesha3 svesm4 flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti ecv afp wfxt
  Model name:             Cortex-A720
    Model:                1
    Thread(s) per core:   1
    Core(s) per socket:   3
    Socket(s):            1
    Stepping:             r0p1
    CPU(s) scaling MHz:   47%
    CPU max MHz:          2600.0000
    CPU min MHz:          357.0000
    BogoMIPS:             49.15
    Flags:                fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp sve2 sveaes svepmull
                          svebitperm svesha3 svesm4 flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti ecv afp wfxt
  Model name:             Cortex-X4
    Model:                1
    Thread(s) per core:   1
    Core(s) per socket:   1
    Socket(s):            1
    CPU(s) scaling MHz:   23%
    CPU max MHz:          3105.0000
    CPU min MHz:          700.0000
    Flags:                fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp sve2 sveaes svepmull
                          svebitperm svesha3 svesm4 flagm2 frint svei8mm svebf16 i8mm bf16 dgh bti ecv afp wfxt
Vulnerabilities:
  Gather data sampling:   Not affected
  Itlb multihit:          Not affected
  L1tf:                   Not affected
  Mds:                    Not affected
  Meltdown:               Not affected
  Mmio stale data:        Not affected
  Reg file data sampling: Not affected
  Retbleed:               Not affected
  Spec rstack overflow:   Not affected
  Spec store bypass:      Mitigation; Speculative Store Bypass disabled via prctl
  Spectre v1:             Mitigation; __user pointer sanitization
  Spectre v2:             Not affected
  Srbds:                  Not affected
  Tsx async abort:        Not affected
"""
hardcoded_lscpu = {
    "phone06": pixel_9_lscpu,
    "phone09": pixel_9_lscpu,
}

def qemu_config(qemu_hostname, num_cpus):
    return CPUInfo(architecture=config.ARCH, vendor="qemu", microarchitectures=[
        Microarchitecture(
            model_name=qemu_hostname,
            num_cores=num_cpus,
            num_sockets=1,
            threads_per_core=1,
            flags=set()
        )
    ])

# TODO(aarch64) and following
def is_floating_point_instr(mnemonic):
    # return true if the instruction is a floating point instruction
    # rg --no-filename ^f rv* | awk '{print $1}' | sort | uniq | grep -v fence | sed -z 's/\n/", "/g' | clip
    return mnemonic in ["fadd.d", "fadd.h", "fadd.q", "fadd.s", "fclass.d", "fclass.h", "fclass.q", "fclass.s", "fcvt.d.h", "fcvt.d.l", "fcvt.d.lu", "fcvt.d.q", "fcvt.d.s", "fcvt.d.w", "fcvt.d.wu", "fcvt.h.d", "fcvt.h.l", "fcvt.h.lu", "fcvt.h.q", "fcvt.h.s", "fcvt.h.w", "fcvt.h.wu", "fcvt.l.d", "fcvt.l.h", "fcvt.l.q", "fcvt.l.s", "fcvt.lu.d", "fcvt.lu.h", "fcvt.lu.q", "fcvt.lu.s", "fcvt.q.d", "fcvt.q.h", "fcvt.q.l", "fcvt.q.lu", "fcvt.q.s", "fcvt.q.w", "fcvt.q.wu", "fcvt.s.d", "fcvt.s.h", "fcvt.s.l", "fcvt.s.lu", "fcvt.s.q", "fcvt.s.w", "fcvt.s.wu", "fcvt.w.d", "fcvt.w.h", "fcvt.w.q", "fcvt.w.s", "fcvt.wu.d", "fcvt.wu.h", "fcvt.wu.q", "fcvt.wu.s", "fdiv.d", "fdiv.h", "fdiv.q", "fdiv.s", "feq.d", "feq.h", "feq.q", "feq.s", "fld", "fle.d", "fle.h", "fle.q", "fle.s", "flh", "flq", "flt.d", "flt.h", "flt.q", "flt.s", "flw", "fmadd.d", "fmadd.h", "fmadd.q", "fmadd.s", "fmax.d", "fmax.h", "fmax.q", "fmax.s", "fmin.d", "fmin.h", "fmin.q", "fmin.s", "fmsub.d", "fmsub.h", "fmsub.q", "fmsub.s", "fmul.d", "fmul.h", "fmul.q", "fmul.s", "fmv.d.x", "fmv.h.x", "fmv.w.x", "fmv.x.d", "fmv.x.h", "fmv.x.w", "fnmadd.d", "fnmadd.h", "fnmadd.q", "fnmadd.s", "fnmsub.d", "fnmsub.h", "fnmsub.q", "fnmsub.s", "fsd", "fsgnj.d", "fsgnj.h", "fsgnj.q", "fsgnj.s", "fsgnjn.d", "fsgnjn.h", "fsgnjn.q", "fsgnjn.s", "fsgnjx.d", "fsgnjx.h", "fsgnjx.q", "fsgnjx.s", "fsh", "fsq", "fsqrt.d", "fsqrt.h", "fsqrt.q", "fsqrt.s", "fsub.d", "fsub.h", "fsub.q", "fsub.s", "fsw"]

# TODO: compressed
# c.ld
def is_memory_read(instr):
    # return true if the instruction is a memory read
    return instr in ("lb", "lh", "lhu", "lw", "lwu", "flw", "ld", "fld")

def is_memory_write(instr):
    # return true if the instruction is a memory read
    return instr in ("sd", "sw", "sh", "sb", "fsd", "fsw")

def repeat_int64(i, n):
    res = 0
    for j in range(n):
        res |= i << (j*64)
    return res

def hexint_presenter(dumper, data):
    return dumper.represent_int(hex(data))
yaml.add_representer(int, hexint_presenter)
def hex_yaml(d):
    return yaml.dump(d, sort_keys=False)

def raise_nofile_limit():
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)

    if soft < hard:
        try:
            resource.setrlimit(resource.RLIMIT_NOFILE, (hard, hard))
        except ValueError as e:
            pass
