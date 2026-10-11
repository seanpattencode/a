#!/bin/sh
# Per-file vault (Sean 10-10): open/ = plain .txt and stays; close encrypts only files newer than .closed into enc/<ts>-<host>/ — append = one file, devices union, uploads never collide; names are public, never a secret.
# Restore anywhere (pw has NO reset — the paper export is the only recovery): read -s VP;export VP;cd enc;for f in $(find . -name '*.enc'|sort);do o=${f#./*/};mkdir -p ${o%/*};openssl enc -d -aes-256-cbc -pbkdf2 -iter 1000000 -pass env:VP -in $f -out ${o%.enc};done
# Change pw: a vault rekey = all of open/ re-encrypted into a new dir; older cloud dirs still open with the old pw, rotate secrets if it leaked.
umask 77;D=${A_VD:-$HOME/a/adata};V=$D/vault/open;EN=$D/vault/enc;X=$D/vault/.exported;C=$D/vault/.closed;G=${A_VG:-a-gdrive2:vault};N=$(uname -n)
O="openssl enc -aes-256-cbc -pbkdf2 -iter 1000000 -pass env:VP"
pw(){ stty -echo 2>/dev/null;printf 'vault pw: ';read -r VP;stty echo 2>/dev/null;echo;export VP;}
case $1 in
_d)r=${2#$EN/*/};o=$V/${r%.enc};[ -e $o ]||{ mkdir -p ${o%/*};$O -d -in $2 -out $o 2>/dev/null||{ rm -f $o;exit 1;};};;
_e)r=${2#$V/};o=$EN/$T/$r.enc;mkdir -p ${o%/*};$O -in $2 -out $o&&$O -d -in $o|cmp -s - $2||{ rm -f $o;echo "x $r";exit 1;};;
export)mkdir -p $V;F=$D/vault/export-$(date +%Y%m%d)-$N.txt
{ echo "VAULT PAPER ROOT $(date +%F) $N — KEEP SAFE: only recovery, NO password reset. Print/copy to paper or USB; delete this file if you wish."
echo 'HANDWRITE (pen only, never typed): google pw ______ github pw ______ 2FA codes ______ vault pw ______'
echo 'Restore: accounts above, gh auth login, gh repo clone seanpattencode/a-git ~/a/adata/git, rclone config reconnect, a vault open (pulls a-gdrive2:vault), re-login claude/codex, remint fleet key (ssh-keygen; a ssh auth)'
for f in ~/.config/gh/hosts.yml ~/.config/ubk.key $D/git/wifi.txt $D/git/login/api_keys.env $D/git/secrets/*;do [ -f "$f" ]&&echo "== $f"&&cat "$f";done
echo '== ssh hosts';grep -hE '^(Name|Host|Password):' $D/git/ssh/*.txt;:;}>$F&&:>$X
echo "exported $(wc -c<$F)B: $F — keep safe, no pw reset; rm once stored if you wish";;
close)[ -f $X ]&&[ -z "$(find $V -newer $X)" ]||{ echo 'x paper export required before encrypt: a vault export';exit 1;}
:>$C.n;L=$(find $V -type f $([ -f $C ]&&echo -newer $C));[ -n "$L" ]||{ rm $C.n;echo 'nothing new since last close';exit 0;}
pw;export T=$(date +%Y%m%dT%H%M%S)-$N;echo "$L"|xargs -P0 -n1 sh $0 _e&&mv $C.n $C&&echo "closed $(echo "$L"|wc -l) files: $EN/$T"||{ rm -rf $EN/$T $C.n;echo 'x verify failed, nothing closed';exit 1;};;
rekey)rm -f $C;echo 'rekey: type the NEW pw';exec sh $0 close;;
open)mkdir -p $V $EN;pw;rclone copy $G $EN 2>/dev/null;n=$(find $V -type f|wc -l);find $EN -name '*.enc'|sort -r|awk -v E=$EN/ '{r=substr($0,length(E)+1);sub("^[^/]*/","",r)}!s[r]++'|xargs -P0 -n1 sh $0 _d;b=$?;[ -f $C ]||touch $C
echo "open: $V +$(( $(find $V -type f|wc -l)-n )) restored$([ $b = 0 ]||echo ', some undecryptable: wrong or old pw')";exit $b;;
push)rclone copy $EN $G&&rclone lsf --dirs-only $G|sort|tail -3||exit 1;;
*)echo 'a vault export|open|close|rekey|push (export required before close; no pw reset)';;
esac
