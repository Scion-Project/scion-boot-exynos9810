#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make the second-stage loader's Image with the images it may start appended (docs/STATUS.md).

  mkloaderimg.py --loader build/starlte/scion-loader.bin --image-end OFFSET \\
      --entry 'ios27:xnu:payload.bin:default' [--entry 'starbite:image:xnu_boot.bin'] ... -o OUT

--image-end  the loader's _image_end minus _start (the Makefile passes it; `make loader-info`).
--entry      NAME:KIND:FILE[:FLAG...]; KIND xnu (a SWEETXNU payload from scion-kernel's
             tools/ios27/herolte/mkpayload.py) or image (an arm64 Image started in place, x0 =
             S-Boot's FDT); FLAGs: default (the menu's default), safe (the boot-loop guard's
             known-good entry). In the order given, at most 8.
--damage     NAME:sha or NAME:crc, for tests only: change one byte of that entry's payload after
             its digests are recorded (sha: the CRC32 is recomputed, so only SHA-256 catches it).
--max-bytes  refuse an output larger than this (starlte: everything must end below S-Boot's
             initrd at 0x89000000, so 0x8f80000 from a 0x80080000 load).

Layout: the loader padded to --image-end, then per entry one 4 KiB page holding the 112-byte SCN2
header (include/boot.h, header_size 4096) and the payload, padded to 4 KiB. The arm64 header's
image_size is set to the whole file. Prints each entry's SHA-256 (the list tools/mkpins.py takes).
"""
import argparse
import hashlib
import struct
import sys
import zlib

MAGIC, VERSION, PAGE = 0x53434E32, 1, 4096
HDR = "<IIIIQQQII32s32s"
KINDS = {"xnu": 1, "image": 2}
FLAGS = {"safe": 1 << 4, "default": 1 << 5}


def record(name, kind, payload, flags, damage=None):
    crc, digest = zlib.crc32(payload) & 0xFFFFFFFF, hashlib.sha256(payload).digest()
    if damage:
        payload = bytearray(payload)
        payload[min(0x100, len(payload) - 1)] ^= 0x5A
        payload = bytes(payload)
        if damage == "sha":
            crc = zlib.crc32(payload) & 0xFFFFFFFF
    hdr = struct.pack(HDR, MAGIC, VERSION, PAGE, flags | (KINDS[kind] << 8), 0, 0, len(payload),
                      crc, 0, digest, name.encode()[:31].ljust(32, b"\0"))
    body = hdr.ljust(PAGE, b"\0") + payload
    return body + b"\0" * (-len(body) % PAGE)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--loader", required=True)
    ap.add_argument("--image-end", required=True, type=lambda x: int(x, 0))
    ap.add_argument("--entry", action="append", default=[])
    ap.add_argument("--damage", action="append", default=[])
    ap.add_argument("--max-bytes", type=lambda x: int(x, 0), default=0)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    img = bytearray(open(a.loader, "rb").read())
    if img[56:60] != b"ARMd":
        sys.exit("mkloaderimg: the loader has no arm64 Image header")
    if a.image_end % PAGE or a.image_end < len(img):
        sys.exit(f"mkloaderimg: --image-end {a.image_end:#x} is not a page boundary at or after the file's end")
    if len(a.entry) > 8:
        sys.exit("mkloaderimg: at most 8 entries")
    img += b"\0" * (a.image_end - len(img))
    damage = {}
    for d in a.damage:
        n, _, how = d.rpartition(":")
        if how not in ("sha", "crc") or not n:
            sys.exit(f"mkloaderimg: --damage NAME:sha|crc (got {d!r})")
        damage[n] = how
    for spec in a.entry:
        parts = spec.split(":")
        if len(parts) < 3 or parts[1] not in KINDS:
            sys.exit(f"mkloaderimg: --entry NAME:KIND:FILE[:FLAG...], KIND xnu or image (got {spec!r})")
        name, kind, path, *fl = parts
        flags = 0
        for f in fl:
            if f not in FLAGS:
                sys.exit(f"mkloaderimg: unknown flag {f!r} (default, safe)")
            flags |= FLAGS[f]
        payload = open(path, "rb").read()
        if not payload:
            sys.exit(f"mkloaderimg: {path} is empty")
        if kind == "xnu" and payload[:8] != b"SWEETXNU":
            sys.exit(f"mkloaderimg: {path} is not a SWEETXNU payload")
        if kind == "image" and payload[56:60] != b"ARMd":
            sys.exit(f"mkloaderimg: {path} is not an arm64 Image")
        img += record(name, kind, payload, flags, damage.pop(name, None))
        if name in [d.rpartition(":")[0] for d in a.damage]:
            print(f"mkloaderimg: {name}: DAMAGED FOR A TEST: the loader must refuse it")
        print(f"mkloaderimg: {name} ({kind}) {len(payload)} bytes sha256 {hashlib.sha256(payload).hexdigest()}")
    if damage:
        sys.exit(f"mkloaderimg: --damage names no entry: {', '.join(damage)}")
    if a.max_bytes and len(img) > a.max_bytes:
        sys.exit(f"mkloaderimg: {len(img)} bytes, more than --max-bytes {a.max_bytes:#x}")
    struct.pack_into("<Q", img, 16, len(img))
    open(a.out, "wb").write(img)
    print(f"mkloaderimg: {a.out}: {len(img)} bytes")


if __name__ == "__main__":
    main()
