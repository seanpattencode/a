/* ── debloat: mac bloat sweeper (vm_bundles, updater, chrome beta/dev, google caches) ── */
static int cmd_debloat(int c,char**v){(void)c;(void)v;
    const char*H=getenv("HOME"),*pp[]={"Library/Application Support/Claude/vm_bundles","Library/Application Support/Google/GoogleUpdater","Library/Application Support/Google/Chrome Beta","Library/Application Support/Google/Chrome Dev","Library/Caches/Google",0};
    char rm[B]="rm -rf",q[512];for(int i=0;pp[i];i++){snprintf(q,512,"%s/%s",H,pp[i]);if(access(q,F_OK))continue;puts(q);strcat(rm," '");strcat(rm,q);strcat(rm,"'");}
    printf("rm -rf? Chrome Beta/Dev lost, caches regen [y/N] ");fflush(stdout);char b[4];if(!fgets(b,4,stdin)||(*b|32)!='y')return 0;
    return system(rm)?1:(puts("\xe2\x9c\x93 freed"),0);}
