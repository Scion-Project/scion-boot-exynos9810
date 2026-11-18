#!/bin/sh
# SPDX-License-Identifier: MIT
# host-test.sh: the loader's plain-C parts on the host (tests/host/codecs-test.c), and SHA-256 /
# CRC32 against Python's hashlib/zlib for every length 0..300 and one of 1,000,000 bytes (every
# padding case of the 64-byte block, and a long run). Needs cc and python3. No target toolchain.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${HOSTCC:-cc}
T=$(mktemp -d "${TMPDIR:-/tmp}/scion-host-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
$CC -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I"$ROOT/include" -I"$ROOT/src/lib" -I"$ROOT/loader" \
	"$ROOT/tests/host/codecs-test.c" "$ROOT/src/lib/crypto.c" "$ROOT/src/lib/verify.c" \
	"$ROOT/loader/policy.c" "$ROOT/loader/xnu.c" -o "$T/codecs-test"
"$T/codecs-test"
# the boot splash (loader/splash.c): the art's pixels, the easter eggs, the build-date variant
$CC -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I"$ROOT/include" -I"$ROOT/src/lib" -I"$ROOT/loader" -I"$ROOT/src/platform" \
	"$ROOT/tests/host/splash-test.c" "$ROOT/loader/splash.c" "$ROOT/src/lib/crypto.c" \
	"$ROOT/src/lib/verify.c" "$ROOT/loader/policy.c" "$ROOT/loader/xnu.c" -o "$T/splash-test"
"$T/splash-test"
for n in $(seq 0 300) 1000000; do
	printf '%s ' "$n"
	"$T/codecs-test" sha "$n"
done > "$T/c.txt"
python3 - "$T/c.txt" <<'PY'
import hashlib, sys, zlib
bad = 0
for line in open(sys.argv[1]):
    n, digest, crc = line.split()
    data = bytes(((i * 7 + (i >> 8)) & 0xff) for i in range(int(n)))
    if digest != hashlib.sha256(data).hexdigest() or int(crc, 16) != zlib.crc32(data):
        print(f"FAIL sha256/crc32 of {n} bytes"); bad += 1
print("sha256/crc32: 302 lengths match hashlib/zlib" if not bad else f"sha256/crc32: {bad} mismatches")
sys.exit(1 if bad else 0)
PY
