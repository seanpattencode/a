"""aio hub - delegates to agents/hub.py"""
import sys, os

def run():
    agents_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'agents')
    if agents_dir not in sys.path: sys.path.insert(0, agents_dir)
    from hub import run as _run; _run()
