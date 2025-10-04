#!/usr/bin/env python3
n = int(input())
factors = []
d = 2
while d * d <= n:
    while n % d == 0:
        factors.append(d)
        n //= d
    d += 1 if d == 2 else 2
print(*(factors + ([n] if n > 1 else [])))
