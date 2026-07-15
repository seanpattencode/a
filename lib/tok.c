/* tok — sum bytes/4 across files/dirs (matches a.c footer convention) */
static int cmd_tok(int c,char**v){perf_disarm();
    if(c<3){ /* bare `a tok`: repo total (tracked files, grouped by top-level entry); cwd files if not in a repo */
        char cm[P*2],out[8192]="";
        snprintf(cm,sizeof(cm),"if t=$(git rev-parse --show-toplevel 2>/dev/null);then echo \"$t (tracked)\";cd \"$t\"&&git ls-files -z|xargs -0 -r wc -c 2>/dev/null;else echo \"$PWD (all files)\";find . -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '*.pyc' -exec wc -c {} + 2>/dev/null;fi|awk 'NR==1&&$1!~/^[0-9]+$/{print;next}$2!=\"total\"{sub(/^\\.\\//,\"\",$2);n=split($2,a,\"/\");d=n>1?a[1]:$2;s[d]+=$1;t+=$1}END{for(d in s)printf \"%%10d  %%s\\n\",s[d]/4,d|\"sort -n\";close(\"sort -n\");printf \"%%10d  total\\n\",t/4}'");
        pcmd(cm,out,8192);fputs(out,stdout);return 0;}
    long total=0;
    for(int i=2;i<c;i++){
        char cm[P*2],buf[64]="";
        snprintf(cm,sizeof(cm),"find '%s' -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '*.pyc' -exec wc -c {} + 2>/dev/null|awk 'END{print $1+0}'",v[i]);
        pcmd(cm,buf,64);long t=atol(buf)/4;total+=t;
        printf("%10ld  %s\n",t,v[i]);}
    if(c>3)printf("%10ld  total\n",total);
    return 0;}
