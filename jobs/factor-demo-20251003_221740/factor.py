import sys
n=int(sys.argv[1])
factors=[]
d=2
while d*d<=n:
    while n%d==0:
        factors.append(d); n//=d
    d+=1 if d==2 else 2
if n>1: factors.append(n)
print(factors)
