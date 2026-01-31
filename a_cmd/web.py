"""aio web - Open web search"""
import sys, subprocess as sp, platform

def run():
    if len(sys.argv) < 3: return print('usage: a web <query>')
    sp.Popen(['open' if platform.system() == 'Darwin' else 'xdg-open', 'https://google.com/search?q=' + '+'.join(sys.argv[2:])], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
