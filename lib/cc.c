/* tcc mob works native macOS arm64 Mach-O—online says no, run to confirm */
static int cmd_cc(int c,char**v){(void)c;(void)v;
    if(!system("command -v tcc>/dev/null 2>&1")){puts("tcc: installed");return 0;}
    puts("tcc: missing");printf("Build+install tcc? [y/N] ");fflush(stdout);
    char b[4];if(!fgets(b,4,stdin)||(*b|32)!='y')return 0;
    char s[B];snprintf(s,B,"set -e;T=%s/tcc$$;git clone -q --depth 1 https://github.com/TinyCC/tinycc.git $T;cd $T;./configure --cc=clang --prefix=$HOME/.local>/dev/null;make -j4>/dev/null;make install>/dev/null;rm -rf $T",TMP);
    if(system(s)){puts("x");return 1;}puts("✓");return 0;}
