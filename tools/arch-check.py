#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The rules of this tree a machine can check (CI runs it; the same idea as scion-kernel's
tools/arch-check.py).

  tools/arch-check.py [--report]

  size    a source file (C, assembly, headers, Python, shell) over CEILING lines fails: split it.
  codec   the files CODECS names are plain C over bytes: they include only freestanding C headers
          and other codec headers, and tools/host-test.sh compiles them (so a host test runs them).
  relocs  nothing under loader/ declares a table of pointers in static data the compiler can
          see as such (`static const char *x[]`): the loader runs wherever S-Boot puts it, so the
          Makefile also refuses any absolute relocation at link time (tools/loader-relocs.sh).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CEILING = 800
SIZED = (".c", ".h", ".S", ".s", ".py", ".sh", ".ld")
CODECS = ["src/lib/crypto.c", "src/lib/verify.c", "src/lib/verify.h", "loader/policy.c",
          "loader/policy.h", "loader/xnu.c", "loader/xnu.h"]
CODEC_OK = {"stddef.h", "stdint.h", "stdbool.h", "limits.h", "boot.h", "lib.h", "verify.h",
            "policy.h", "xnu.h"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
PTR_TABLE = re.compile(r'static\s+const\s+char\s*\*\s*(const\s+)?\w+\s*\[')


def files():
    out = subprocess.run(["git", "-C", ROOT, "ls-files"], capture_output=True, text=True, check=True)
    return [f for f in out.stdout.splitlines() if f]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    bad = []
    tracked = files()
    for f in tracked:
        if not f.endswith(SIZED):
            continue
        n = open(os.path.join(ROOT, f), "rb").read().count(b"\n")
        if n > CEILING:
            bad.append(f"size: {f} has {n} lines (ceiling {CEILING})")
        elif a.report and n > CEILING * 3 // 4:
            print(f"note: {f} {n} lines")
    host = open(os.path.join(ROOT, "tools/host-test.sh")).read()
    for f in CODECS:
        if f not in tracked:
            bad.append(f"codec: {f} is listed but not in git")
            continue
        src = open(os.path.join(ROOT, f)).read()
        for inc in INCLUDE.findall(src):
            if os.path.basename(inc) not in CODEC_OK:
                bad.append(f"codec: {f} includes {inc}")
        if f.endswith(".c") and f not in host:
            bad.append(f"codec: {f} is not compiled by tools/host-test.sh")
    for f in tracked:
        if f.startswith("loader/") and f.endswith(".c"):
            for m in PTR_TABLE.finditer(open(os.path.join(ROOT, f)).read()):
                bad.append(f"relocs: {f}: a table of pointers ({m.group(0)}...)")
    for b in bad:
        print(b)
    print("arch-check: ok" if not bad else f"arch-check: {len(bad)} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
