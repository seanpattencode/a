import os,sys;from _common import init_db,load_cfg,create_sess
def run():
    if'install'in sys.argv:os.system('curl -sL https://raw.githubusercontent.com/seanpattencode/editor/main/e.c|clang -xc -Wno-everything -o ~/.local/bin/e -');return
    init_db();c=load_cfg();os.execvp('e',['e','.'])if'TMUX'in os.environ else(create_sess('edit',os.getcwd(),'e .',c),os.execvp('tmux',['tmux','attach','-t','edit']))
