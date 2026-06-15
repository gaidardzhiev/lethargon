#!/bin/sh

[ ! -f lethargon ] && make; { n() { printf "\n\n\n"; }; run() { ./lethargon "$1" -o "${1%.lt}.out" && "${1%.lt}.out" && strace "${1%.lt}.out" && n; }; n && for f in src/*.lt stage1/*.lt; do [ -f "$f" ] && run "$f"; done && n; }
