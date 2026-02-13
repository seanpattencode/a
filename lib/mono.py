"""a mono - Generate monolith for reading"""
import sys, os
from glob import glob as G

def run():
    d = os.path.dirname(os.path.realpath(__file__))
    p = os.path.expanduser("~/.local/share/a/a_mono.py")
    open(p, 'w').write('\n\n'.join(f"# === {f} ===\n" + open(f).read() for f in sorted(G(d + '/*.py'))))
    print(p)

if __name__ == '__main__': run()
