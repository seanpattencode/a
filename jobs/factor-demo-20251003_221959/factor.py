import sys
n=int(sys.argv[1])
f=[]
d=2
while d*d<=n:
    if n%d:d+=1
    else:f.append(str(d));n//=d
if n>1:f.append(str(n))
print(" ".join(f) or str(n))
