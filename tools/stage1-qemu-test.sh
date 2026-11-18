#!/bin/sh
# SPDX-License-Identifier: MIT
# stage1-qemu-test.sh: stage 1's QEMU smoke test (README "Build"), as CI runs it: boot
# build/qemu/scion-boot.elf at EL3 for at most 20 s and require the stage-2 stub's
# "hand-off contract satisfied". Build it first: make PLATFORM=qemu.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ELF=$ROOT/build/qemu/scion-boot.elf
[ -f "$ELF" ] || { echo "stage1-qemu-test: no $ELF (make PLATFORM=qemu)" >&2; exit 1; }
LOG=$(mktemp "${TMPDIR:-/tmp}/stage1-qemu.XXXXXX")
trap 'rm -f "$LOG"' EXIT
timeout 20 qemu-system-aarch64 -M virt,secure=on,virtualization=on -cpu cortex-a57 -m 2G \
	-display none -monitor none -nic none -serial stdio -kernel "$ELF" < /dev/null > "$LOG" 2>&1 || true
for want in "crypto: CRC32 + SHA-256 self-test PASS" "loader: verified 'stage2_stub'" \
	"hand-off contract satisfied"; do
	if ! grep -q "$want" "$LOG"; then
		echo "stage1-qemu-test: FAIL: no \"$want\"" >&2
		cat "$LOG" >&2
		exit 1
	fi
done
echo "stage1-qemu-test: PASS"
