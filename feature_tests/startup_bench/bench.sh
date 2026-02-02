#!/bin/bash
cd "$(dirname "$0")"
gcc -O3 -w -o hello_c hello.c
as -o hello.o hello.s && ld -o hello_asm hello.o
echo "hello" > /tmp/h.txt
N=100

t() { local s=$(date +%s%N); for i in $(seq $N); do "$@" >/dev/null; done; echo "$(( ($(date +%s%N) - s) / 1000000 ))ms ($N runs)"; }

echo "C:         $(t ./hello_c)"
echo "ASM:       $(t ./hello_asm)"
echo "bash echo: $(t bash -c 'echo hello')"
echo "bash cat:  $(t cat /tmp/h.txt)"
echo "builtin:   $(t echo hello)"
