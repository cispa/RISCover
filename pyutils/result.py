#!/usr/bin/env python3

# This file defines the Result class. Result stores signum, final registers etc.
# Further, the file contains methods to parse binary data into a Result.
import signal
import struct

import pyutils.config as config

import sys, os
sys.path.append(os.path.dirname(__file__))

from util import VEC_REG_SIZE, vec, regs_mapping, sig_to_str, sig_to_color_str

# Number of bytes captured per memory change (must match client CHECK_MEM_CUT_AT)
MEM_CUT_AT = 16
from pyutils.inp import Input

class Result:
    def __init__(self, signum, cycle_diff, instret_diff, regs, si_addr, si_pc, si_code, client, mem_diffs):
        self.signum = signum
        self.cycle_diff = cycle_diff
        self.instret_diff = instret_diff
        self.regs = regs
        self.si_addr = si_addr
        self.si_pc = si_pc
        self.si_code = si_code
        self.mem_diffs = mem_diffs

        self.client = client
        self.real_client = client

        self.initial_regs = None

    def reg_diffs(self):
        diffs = []
        for reg in self.regs:
            initial=self.initial_regs[reg]
            if self.regs[reg] != initial:
                diffs += [(reg, initial, self.regs[reg])]
        return diffs

    def __str__(self, **kwargs):
        if "color" in kwargs and kwargs["color"] == False:
            out=f"signum:     {sig_to_str(self.signum).rjust(20)}"
        else:
            out=f"signum:     {sig_to_color_str(self.signum).rjust(20)}"
        out+="\n"
        if self.signum != 0:
            out+=f"si_addr:      {f'0x{self.si_addr:x}'.rjust(18)}\n"
            out+=f"si_pc:        {f'0x{self.si_pc:x}'.rjust(18)}\n"
            out+=f"si_code:      {f'0x{self.si_code:x}'.rjust(18)}\n"
        if self.cycle_diff:
            out+=f"cycle_diff:           {self.cycle_diff:10}\n"
        if self.instret_diff:
            out+=f"instret_diff:         {self.instret_diff:10}\n"

        for reg, initial, now in self.reg_diffs():
            if reg in vec:
                out+=f"  {reg.rjust(4)}:       0x{initial:0{VEC_REG_SIZE*2}x} -> 0x{now:0{VEC_REG_SIZE*2}x}\n"
            else:
                out+=f"  {reg.rjust(4)}:       0x{initial:016x} -> 0x{now:016x}\n"

        if self.mem_diffs != None:
            out+="mem_diffs:\n"
            for d in self.mem_diffs:
                # Tuple format: (start, n_full, val_prefix, checksum)
                start, n, val, checksum = d
                n_cap = n if n < MEM_CUT_AT else MEM_CUT_AT
                suffix = "" if n <= MEM_CUT_AT else f" (cut_at={MEM_CUT_AT})"
                out+=f"  0x{start:016x} 0x{val:0{2*n_cap}x} ({n} bytes, checksum=0x{checksum:08x}){suffix}\n"

        return out.strip()

    def to_dict(self):
        d = {
            'signum': self.signum,
            'si_addr': self.si_addr,
            'si_pc': self.si_pc,
            'si_code': self.si_code,
        }
        if self.cycle_diff:
            d['cycle_diff'] = self.cycle_diff
        if self.instret_diff:
            d['instret_diff'] = self.instret_diff
        d['regs_after'] = self.regs
        if self.mem_diffs != None:
            d['mem_diffs'] = [
                { "start": start, "n": n, "val": val, "checksum": checksum }
                for start, n, val, checksum in self.mem_diffs
            ]
        return d

    @classmethod
    def from_dict(cls, d):
        signum = d.get('signum')
        si_addr = d.get('si_addr')
        si_pc = d.get('si_pc')
        si_code = d.get('si_code')
        regs_after = d.get('regs_after')

        cycle_diff = d.get('cycle_diff', None)
        instret_diff = d.get('instret_diff', None)

        if 'mem_diffs' in d:
            mem_diffs = [
                (entry['start'], entry['n'], entry['val'], entry['checksum'])
                for entry in d['mem_diffs']
            ]
        else:
            mem_diffs = None

        return cls(
            signum=signum,
            si_addr=si_addr,
            si_pc=si_pc,
            si_code=si_code,
            cycle_diff=cycle_diff,
            instret_diff=instret_diff,
            regs=regs_after,
            mem_diffs=mem_diffs,
            client = None,
        )

    def __eq__(self, other):
        if not isinstance(other, type(self)):
            return False

        if self.signum != other.signum:
            return False
        if self.si_addr != other.si_addr:
            return False
        if self.si_pc != other.si_pc:
            return False
        if self.si_code != other.si_code:
            return False
        if self.mem_diffs is not None:
            if self.mem_diffs != other.mem_diffs:
                return False
        if self.regs != other.regs:
            return False

        return True

    def similar(self, other) -> bool:
        if self.signum != other.signum:
            return False
        if self.si_code != other.si_code:
            return False
        if (len(self.regs) == 0) != (len(other.regs) == 0):
            return False
        if self.mem_diffs is not None:
            if (len(self.mem_diffs) == 0) != (len(other.mem_diffs) == 0):
                return False

        return True

    def __hash__(self):
        regs_hash = frozenset(self.regs.items())
        mem_diffs_hash = hash(tuple(self.mem_diffs if self.mem_diffs else []))
        return hash((
            self.signum,
            self.si_addr,
            self.si_pc,
            self.si_code,
            regs_hash,
            mem_diffs_hash,
        ))

    def diff(self, other, no_regs=False):
        diffs=set()
        if self.signum != other.signum:
            diffs.add("signum")
        if self.signum != 0 and other.signum != 0:
            if self.si_addr != other.si_addr:
                diffs.add("si_addr")
            if self.si_pc != other.si_pc:
                diffs.add("si_pc")
            if self.si_code != other.si_code:
                diffs.add("si_code")
        if not no_regs:
            for reg in set(list(self.regs)+list(other.regs)):
                if reg not in other.regs or reg not in self.regs or self.regs[reg] != other.regs[reg]:
                    diffs.add(str(reg))
        if self.mem_diffs != None or other.mem_diffs != None:
            if self.mem_diffs != other.mem_diffs:
                diffs.add("mem")

        return diffs

# NOTE: This class is for, e.g., discarding parts of the result if
# a specific generic condition matches.
# Its faster than a filtering function because we change the actual
# hashes.
class LenientResult(Result):
    def __init__(self, result: Result):

        # Make SIGSEGV and SIGBUS be treated as equal for similarity because there
        # is no way to change what the kernel does and its no big difference.
        self.orig_signum = result.signum
        new_signum = result.signum
        if new_signum == signal.SIGBUS:
            new_signum = signal.SIGSEGV

        # Remove register and memory changes when there is an ALARM.
        # Imagine an infinite loop with an increment in each step.
        # Register values then depend on CPU speed and scheduling.
        # NOTE: This does not hide SIGALRM in general, only if every client
        # has the alarm.
        if new_signum == signal.SIGALRM:
            super().__init__(new_signum, result.cycle_diff, result.instret_diff, {}, 0, 0, 0, result.client, [])
        else:
            super().__init__(new_signum, result.cycle_diff, result.instret_diff, result.regs, result.si_addr, result.si_pc, result.si_code, result.client, result.mem_diffs)

class FilteredResult(Result):
    def __init__(self, result: Result, inp: Input, filt):
        self.result = result
        self.inp = inp
        self.filt = filt

    def __eq__(self, other):
        if not isinstance(other, Result):
            return False

        if self.filt(self.inp, self, other):
            return True

        return super().__eq__(other)

    def __hash__(self):
        # We can't compute a hash here since filter skews
        # what equal above returns. Performance-wise buckets seem to be fast enough.
        return 0

    def __getattr__(self, name):
        """Forward any method or attribute calls to result."""
        return getattr(self.result, name)

class MultiResult:
    def __init__(self, results: list[Result]):
        self.results = results

    # Forward client and real_client changes to the underlying results
    def __setattr__(self, name, value):
        if name in ('client', 'real_client'):
            for result in self.results:
                setattr(result, name, value)
        else:
            super().__setattr__(name, value)

def parse_one_result(data, client) -> tuple[Result, int]:
    signum_data = data[0:1]
    r = 1
    if not signum_data:
        return None, 0
    try:
        signum = struct.unpack("B", signum_data)[0]
        if config.META:
            cycle_diff = struct.unpack("H", data[r:r+2])[0]
            r+=2
            match config.ARCH:
                case "riscv64":
                    instret_diff = struct.unpack("H", data[r:r+2])[0]
                    r+=2
                case _:
                    instret_diff = None
        else:
            cycle_diff = None
            instret_diff = None

        regs_diff = struct.unpack("B", data[r:r+1])[0]
        r+=1

        regs = {}
        for _ in range(regs_diff):
            reg_index = struct.unpack("B", data[r:r+1])[0]
            r+=1
            reg = regs_mapping[reg_index]
            if reg.startswith("v"):
                reg_size = VEC_REG_SIZE
                reg_value = int.from_bytes(data[r:r+reg_size], "little")
            else:
                reg_size = 8
                reg_value = struct.unpack("Q", data[r:r+reg_size])[0]
            r+=reg_size

            regs[reg]=reg_value

        si_addr = 0
        si_pc = 0
        if signum != 0:
            si_addr = struct.unpack("Q", data[r:r+8])[0]
            r+=8
            si_pc = struct.unpack("Q", data[r:r+8])[0]
            r+=8

        si_code = 0
        if signum != 0:
            si_code = struct.unpack("I", data[r:r+4])[0]
            r+=4

        mem_diffs = None
        if config.CHECK_MEM:
            mem_diffs_n = struct.unpack("B", data[r:r+1])[0]
            r+=1
            mem_diffs = []
            for _ in range(mem_diffs_n):
                start = struct.unpack("Q", data[r:r+8])[0]
                r+=8
                n = struct.unpack("I", data[r:r+4])[0]
                r+=4
                n_cap = n if n < MEM_CUT_AT else MEM_CUT_AT
                # Value prefix as little-endian integer of n_cap bytes
                val = int.from_bytes(data[r:r+n_cap], byteorder="little") if n_cap > 0 else 0
                r+=n_cap
                checksum = struct.unpack("I", data[r:r+4])[0]
                r+=4
                mem_diffs += [(start, n, val, checksum)]

    except struct.error:
        # TODO: exit entire process tree or implement some other way to report back
        # probably just pass None back and then error out there
        # maybe we can also just remove the try catch
        import sys, os
        print("Error: struct error", file=sys.stderr)
        os._exit(1)

    return Result(signum, cycle_diff, instret_diff, regs, si_addr, si_pc, si_code, client, mem_diffs), r

def parse_one_result_full_seq(data, client) -> tuple[Result, int]:
    r = 0
    seq_len = struct.unpack("B", data[r:r+1])[0]
    r += 1

    # print("seq_len:", seq_len)

    results = []
    for _ in range(seq_len):
        result, read = parse_one_result(data[r:], client)
        results += [result]
        r += read

    return MultiResult(results), r

def parse_results(data: bytes, client) -> list[Result]:
    aread = 0
    results_read = 0
    results = []
    # TODO: we do this here since it got slow otherwise
    while True:

        # print("aread", aread)
        # print("len adata", len(data))
        # print("results read", results_read)
        full_seq = struct.unpack("B", data[aread:aread+1])[0] == 1
        aread += 1

        # print("full seq:", full_seq)

        if not full_seq:
            # TODO: this is also slow, we should probably recv all at the beginning and use one struct.unpack
            # TODO: at least make sure that they are collected in an iterator (seems to be faster)
            result, read = parse_one_result(data[aread:], client)
        else:
            result, read = parse_one_result_full_seq(data[aread:], client)
        results += [result]
        aread += read
        if not result:
            import sys, os
            # TODO: exit entire process tree or implement some other way to report back
            # probably just pass None back and then error out there
            print("Error: parsing Result failed", file=sys.stderr)
            os._exit(1)
        assert(aread <= len(data))
        results_read += 1
        if aread == len(data):
            break

    return results
