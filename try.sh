#!/bin/sh

[ ! -f "${L}" ] && make && printf "\n\n\n" && for f in src/*.lt; do [ -f "$f" ] && echo "*** $f ***" && ./lethargon "$f" -o "${f%.lt}.out" && "${f%.lt}.out" && strace "${f%.lt}.out" && echo "*** done ***" && printf "\n\n\n" || echo "*** failed ***"; done && printf "\n\n\n" && for f in stage1/*.lt; do [ -f "$f" ] && echo "*** $f ***" && ./lethargon "$f" -o "${f%.lt}.out" && "${f%.lt}.out" && strace "${f%.lt}.out" && echo "*** done ***" && printf "\n\n\n" || echo "*** failed ***" && printf "\n\n\n"; done
