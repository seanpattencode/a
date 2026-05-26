#!/bin/sh
[ -f ~/.config/gh/hosts.yml ] && [ -n "$1" ] || { echo "a clone <ssh-host>"; exit 1; }
B=$(base64 -w0 ~/.config/gh/hosts.yml)
exec a ssh "$1" "mkdir -p ~/.config/gh;echo $B|base64 -d>~/.config/gh/hosts.yml;chmod 600 ~/.config/gh/hosts.yml;curl -fsSL https://raw.githubusercontent.com/seanpattencode/a/main/a.c|sh;gh auth setup-git;rm -rf ~/a/adata/git;gh repo clone seanpattencode/a-git ~/a/adata/git"
