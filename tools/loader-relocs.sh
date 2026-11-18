#!/bin/sh
# SPDX-License-Identifier: MIT
# loader-relocs.sh READELF OBJ...: fail when an object of the second-stage loader has an absolute
# 64-bit relocation (a pointer in static data). The loader runs wherever S-Boot puts it.
readelf=$1
shift
bad=$($readelf -r "$@" | grep -E 'ABS64' || true)
[ -z "$bad" ] && exit 0
echo "loader: absolute relocation(s) found; build tables of values, not pointers:" >&2
for o in "$@"; do
	if $readelf -r "$o" | grep -q ABS64; then
		echo "  $o:" >&2
		$readelf -r "$o" | grep ABS64 >&2
	fi
done
exit 1
