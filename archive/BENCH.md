## Hardening flag benchmark

Run on Ubuntu to measure compile-time cost of each flag.

```sh
cd ~/projects/a && git pull
B="clang -DSRC=. -O3 -march=native -flto -w -o a a.c"

t() { rm -f a; S=$(date +%s%N); eval "$2" 2>err.tmp; rc=$?; E=$(date +%s%N)
  printf "%-22s %4dms  %s\n" "$1" "$(( (E-S)/1000000 ))" \
    "$([ $rc -eq 0 ] && echo OK || head -1 err.tmp)"; rm -f err.tmp; }

t "baseline"           "$B"
t "_FORTIFY_SOURCE=3"  "$B -D_FORTIFY_SOURCE=3"
t "safe-stack"         "$B -fsanitize=safe-stack"
t "cfi"                "$B -fsanitize=cfi -fvisibility=hidden"
t "cf-protection=full" "$B -fcf-protection=full"
t "clang --analyze"    "clang -DSRC=. --analyze a.c"
t "make (full build)"  "make clean >/dev/null 2>&1; make"
rm -f a
```

Each flag is tested alone against baseline. `--analyze` is a static analysis
pass (closest to Rust's borrow checker — catches use-after-free, null deref,
leaks at compile time). The others are runtime hardening with near-zero overhead.
