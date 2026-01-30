"""aio e - Open editor"""
import os
from . _common import init_db, load_cfg, create_sess

def run():
    init_db()
    cfg = load_cfg()
    if 'TMUX' in os.environ:
        os.execvp('e', ['e', '.'])
    else:
        create_sess('edit', os.getcwd(), 'e .', cfg)
        os.execvp('tmux', ['tmux', 'attach', '-t', 'edit'])
