"""aio web - Open web search"""
import sys, subprocess as sp, platform

def run():
    cmd = 'open' if platform.system() == 'Darwin' else 'xdg-open'
    url = 'https://google.com/search?q=' + '+'.join(sys.argv[2:]) if len(sys.argv) >= 3 else 'https://google.com'
    sp.Popen([cmd, url], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
