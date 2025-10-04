import sys
n=m=int(sys.argv[1])
f=[]
d=2
while d*d<=n:
    while n%d==0:f.append(d);n//=d
    d+=1
if n>1:f.append(n)
print("1" if m==1 else "*".join(map(str,f)))
