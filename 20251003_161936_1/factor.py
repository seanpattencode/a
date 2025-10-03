#!/usr/bin/env python3
n=int(input())
f=2
r=[]
while f*f<=n:
    while n%f==0:
        r.append(f);n//=f
    f+=1 if f==2 else 2
if n>1:r.append(n)
print(*r)
