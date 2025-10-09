#!/usr/bin/env python3
import gdb

# TODO: this all does not reliably work, going to printf
# debugging again...
# Tried with reraising but didnt work out

# FIXME: currently on works on arm with hardcoded addresses

# Usage:
# start_clients.sh --debug lab65 lab39 lab09 -- server/diffuzz-server.py --vector --single-client -- --verbose
# or
# init-build-repro client/misc/aarch64-simple-store.yaml -- --single-threaded --debug
#
# gdb -x debug_script.py -ex 'run 192.168.10.165 1337' <binary>
# or
# gdb -x debug_script.py <binary>
# or
# source debug_script.py

# FIXME: enable this only when running multi threaded (untested)
# gdb.execute("set follow-fork-mode child", to_string=True)

def check_runner_code_start_in_backtrace():
    """Check if 'runner_code_start' appears in any frame of the backtrace."""
    frame = gdb.newest_frame()
    while frame is not None:
        function = frame.name()
        if function and "runner_code_start" in function:
            return True
        frame = frame.older()
    return False

def ignore_signal_if_in_range(event):
    # Process only signal events.
    if isinstance(event, gdb.SignalEvent):
        frame = gdb.selected_frame()
        if frame is None:
            return
        try:
            pc = int(frame.pc())
        except Exception as e:
            gdb.write("Error reading PC: {}\n".format(e))
            return

        # TODO: hardcoded to values on ARM
        # Define the PC range.
        low_bound = 0x00000024de6e2000
        high_bound = 0x00000024de6e4000

        in_range = low_bound <= pc <= high_bound
        in_backtrace = check_runner_code_start_in_backtrace()

        if in_range or in_backtrace:
            gdb.write("Ignoring signal {} at PC 0x{:x} (in_range: {}, runner_code_start in backtrace: {})\n".format(
                event.stop_signal, pc, in_range, in_backtrace))
            gdb.execute("continue", to_string=True)

# Connect the event handler.
gdb.events.stop.connect(ignore_signal_if_in_range)

gdb.write("Signal ignoring handler installed.\n")
