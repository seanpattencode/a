"""a mono - Generate monolith for reading"""
import sys, os
from glob import glob as G

def run():
    d = os.path.dirname(os.path.realpath(__file__))
    p = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'adata', 'local', 'a_mono.py')
    open(p, 'w').write('\n\n'.join(f"# === {f} ===\n" + open(f).read() for f in sorted(G(d + '/*.py'))))
    print(p)

if __name__ == '__main__': run()
