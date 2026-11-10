#!/usr/bin/env python3
"""
mkstage2.py — Wrap a raw AArch64 stage-2 binary in a struct scion_stage2_hdr
('SCN2') with IEEE 802.3 CRC32 and FIPS 180-4 SHA-256 integrity digests.

Header layout (112 bytes, little-endian, matches include/boot.h):
  uint32_t magic;          // 0x53434E32 ('SCN2')
  uint32_t version;        // 1
  uint32_t header_size;    // 112
  uint32_t flags;          // SCION_S2_FLAG_*
  uint64_t load_addr;      // destination physical address (0 = in-place)
  uint64_t entry_offset;   // byte offset from payload start to entry point
  uint64_t image_size;     // payload size in bytes
  uint32_t crc32;          // IEEE 802.3 CRC32 of payload
  uint32_t _pad0;
  uint8_t  sha256[32];     // SHA-256 digest of payload
  char     name[32];       // NUL-padded descriptive name
"""
import argparse
import hashlib
import os
import struct
import sys
import zlib

SCION_STAGE2_MAGIC   = 0x53434E32
SCION_STAGE2_VERSION = 1
HEADER_FORMAT        = "<IIIIQQQII32s32s"
HEADER_SIZE          = struct.calcsize(HEADER_FORMAT)


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a Stage-2 binary with a scion_stage2_hdr ('SCN2').")
    parser.add_argument("-i", "--input", required=True, help="Input raw stage-2 binary")
    parser.add_argument("-o", "--output", required=True, help="Output .scn2 image")
    parser.add_argument("-l", "--load-addr", type=lambda x: int(x, 0), default=0,
                        help="Destination physical load address (default: 0 = in-place)")
    parser.add_argument("-e", "--entry-offset", type=lambda x: int(x, 0), default=0,
                        help="Entry offset from payload start (default: 0)")
    parser.add_argument("-f", "--flags", type=lambda x: int(x, 0), default=0x2,
                        help="Header flags (default: 0x2 = SCION_S2_FLAG_PIC)")
    parser.add_argument("-n", "--name", default="", help="Image name (max 31 chars)")
    args = parser.parse_args()

    assert HEADER_SIZE == 112, f"Expected 112-byte header, got {HEADER_SIZE}"

    with open(args.input, "rb") as f:
        payload = f.read()

    if not payload:
        print("Error: input binary is empty", file=sys.stderr)
        return 1

    crc = zlib.crc32(payload) & 0xFFFFFFFF
    digest = hashlib.sha256(payload).digest()
    name_str = args.name or os.path.basename(args.input)
    name_bytes = name_str.encode("utf-8")[:31].ljust(32, b"\x00")

    hdr = struct.pack(
        HEADER_FORMAT,
        SCION_STAGE2_MAGIC,
        SCION_STAGE2_VERSION,
        HEADER_SIZE,
        args.flags,
        args.load_addr,
        args.entry_offset,
        len(payload),
        crc,
        0,
        digest,
        name_bytes,
    )

    with open(args.output, "wb") as f:
        f.write(hdr)
        f.write(payload)

    print(
        f"mkstage2: wrote '{args.output}' ({len(payload)}B payload + {HEADER_SIZE}B hdr, "
        f"crc32=0x{crc:08x}, sha256={digest.hex()[:16]}...)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
