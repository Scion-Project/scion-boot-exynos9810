#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A SWEETXNU payload (the layout of scion-kernel's tools/ios27/herolte/mkpayload.py, version 1)
around a test program for tools/qemu-test.py: one segment, a fake device tree.

  mkfakexnu.py BIN MEMSIZE OUT [--cmdline S] [--phys 0x60000000]
MEMSIZE: the program's size in memory (file + .bss).
"""
import argparse
import struct

PAGE = 0x4000
VIRT = 0xfffffff007000000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("memsize", type=lambda x: int(x, 0))
    ap.add_argument("out")
    ap.add_argument("--cmdline", default="-v serial=3 debug=0x8")
    ap.add_argument("--phys", type=lambda x: int(x, 0), default=0x60000000)
    a = ap.parse_args()
    code = open(a.bin, "rb").read()
    seg_va = VIRT + PAGE
    vmsize = (max(a.memsize, len(code)) + PAGE - 1) // PAGE * PAGE
    kernel_end = seg_va + vmsize
    adt = b"FAKEADT!" + bytes(range(256)) * 4
    head = 0x2a8 + 32
    code_off = (head + 0xfff) // 0x1000 * 0x1000
    adt_off = code_off + (len(code) + 0xfff) // 0x1000 * 0x1000
    line = a.cmdline.encode()
    assert len(line) < 608
    hdr = struct.pack("<8sII", b"SWEETXNU", 1, 1)
    hdr += struct.pack("<QQQQQQQ", VIRT, seg_va, adt_off, len(adt), kernel_end, a.phys, 0x10000000)
    hdr += line.ljust(608, b"\0")
    hdr += struct.pack("<QQQQ", seg_va, vmsize, code_off, len(code))
    out = hdr.ljust(code_off, b"\0") + code
    out = out.ljust(adt_off, b"\0") + adt
    open(a.out, "wb").write(out)


if __name__ == "__main__":
    main()
