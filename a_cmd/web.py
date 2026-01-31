"""aio web - Open web search"""
import sys, os

def run():
    if len(sys.argv) > 2: os.system('xdg-open "https://google.com/search?q=' + '+'.join(sys.argv[2:]) + '"')
    else: os.system('gtk-launch $(xdg-settings get default-web-browser)')
