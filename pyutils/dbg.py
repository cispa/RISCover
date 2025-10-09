#!/usr/bin/env python3
"""
A lightweight Python debug header.
Usage:
    export DEBUG=1            # enable at startup
    from dbg import dbg, dbg_enable
    dbg_enable()              # enable at runtime
    dbg_disable()             # disable at runtime

    from dbg import dbg
    dbg()                     # prints "(reached)" with file:line:function
    dbg(x)                    # prints "x = <value>" (or just "<value>" if literal)
    dbg(x, y, z)              # prints multiple args with "; " separators
"""
import os
import sys
import socket
import inspect
import ast
import re

# Global debug flag: set via environment or at runtime
DEBUG = os.getenv("DEBUG", "") in ("1", "true", "True")


def dbg_enable():
    """Enable debug printing."""
    global DEBUG
    DEBUG = True


def dbg_disable():
    """Disable debug printing."""
    global DEBUG
    DEBUG = False


def dbg(*args):
    """Debug-print arguments with file:line:function context."""
    if not DEBUG:
        return

    # Inspect caller
    frame = inspect.currentframe().f_back
    info = inspect.getframeinfo(frame)
    filename = info.filename
    lineno   = info.lineno
    col      = info.positions.col_offset + 1
    funcname = frame.f_code.co_name

    # Compute absolute and relative paths
    fullpath = os.path.realpath(filename)
    relpath = os.path.relpath(filename)
    hostname = socket.gethostname()

    # Build ANSI hyperlink prefix: file://hostname/fullpath in link, relpath in label
    link_target = f"file://{hostname}{fullpath}:{lineno}:{col}"
    HYP_START = f"\x1b]8;;{link_target}\x1b\\"
    HYP_END   = "\x1b]8;;\x1b\\"

    # Align lineno:col field to width of "9999:999"
    pos_text = f"{lineno}:{col}"
    max_width = len("9999:999")
    padding = " " * max(0, max_width - len(pos_text))

    # No-arg: just mark reached
    if len(args) == 0:
        prefix = f"{HYP_START}[{relpath}:{pos_text}{padding} {funcname}]{HYP_END}"
        print(f"{prefix} (reached)", file=sys.stderr, flush=True)
        return

    # Try to recover the source-expression texts for each argument
    code_line = info.code_context[0].strip() if info.code_context else ""
    m = re.search(r'dbg\s*\(\s*(.*)\s*\)', code_line)
    arg_texts = []
    if m:
        args_str = m.group(1)
        try:
            # Parse a dummy call to extract AST arg nodes
            mod = ast.parse(f"f({args_str})")
            call = mod.body[0].value  # ast.Call
            for node in call.args:
                txt = ast.get_source_segment(f"f({args_str})", node)
                arg_texts.append(txt)
        except Exception:
            arg_texts = [None] * len(args)
    else:
        arg_texts = [None] * len(args)

    # Format each arg: name = value or just value if literal or no name
    parts = []
    for name, val in zip(arg_texts, args):
        if name:
            parts.append(f"{name} = {val!r}")
        else:
            parts.append(f"{val!r}")

    # Print to stderr
    prefix = f"{HYP_START}[{relpath}:{pos_text}{padding} {funcname}]{HYP_END}"
    print(prefix + " " + "; ".join(parts), file=sys.stderr, flush=True)
