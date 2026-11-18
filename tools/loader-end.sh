#!/bin/sh
# SPDX-License-Identifier: MIT
# loader-end.sh NM ELF [END_SYMBOL]: print END_SYMBOL - _start (default _image_end: where
# tools/mkloaderimg.py appends the SCN2 records to the loader), as 0x... .
nm=$1
sym=${3:-_image_end}
s=$($nm "$2" | awk '$3 == "_start" { print $1 }')
e=$($nm "$2" | awk -v sym="$sym" '$3 == sym { print $1 }')
if [ -z "$s" ] || [ -z "$e" ]; then
	echo "loader-end: no _start/$sym in $2" >&2
	exit 1
fi
printf '0x%x\n' $((0x$e - 0x$s))
