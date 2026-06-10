#!/bin/sh
#Copyright (C) 2026 Ivan Gaydardzhiev
#Licensed under the GPL-3.0-only

G='\033[0;32m'
R='\033[0;31m'
N='\033[0m'
L='lethargon'

[ ! -f "${L}" ] && make

fprint() {
	printf "[%s] Test: %-25s\nExpected: %s\nCaptured: %s %b\n\n" "$(date '+%Y-%m-%d %H:%M:%S')" "${1}" "${2}" "${3}" "${4}"
}

fth() {
	./"${L}" src/hello.lt -o src/hello.out
	captured=$(src/hello.out)
	expected="Hello from the Lethargon World!"
	[ "${captured}" = "${expected}" ] && {
		fprint "Print Hello World" "${expected}" "${captured}" "\n${G}PASSED${N}\n";
		return 0;
	} || {
		fprint "Print Hello World" "${expected}" "${captured}" "\n${R}FAILED${N}\n";
		return 2;
	}
}

{ fth; ret="${?}"; } || exit 1

[ "${ret}" -eq 0 ] 2>/dev/null || printf "%s\n" "${ret}"
