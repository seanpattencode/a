/* m — chat+agentic loop. files (no /tmp; verify externally):
 * m/{m.txt,i.txt} (gh repo "m", root-level) · adata/local/m_{status,panel,pty,st,file,editorpid,err.log,inXXXXXX}
 * archive: trim in place + git commit with preview in subject. retrieval via git log/show. no marker, no archive/ dir.
 * debug: tail m/m.txt ; tmux capture-pane -t <id> -p ; cat adata/local/m_*
 * e --nosb: skip e's internal scrollbar — tmux pane-scrollbars handles vertical scroll, avoids fold-aware sb math
 * sysprompt: hardcoded M_SYS (m-only cmd contract). i.txt = single user-editable file, loaded by all agents. */

static const char *M_SYS = "`a m` chat: emit <cmd>command</cmd> to run in pty (cwd=m). After </cmd>, STOP. Markdown ```bash/```sh blocks are for showing code only (NEVER executed). No Read/Write/Bash/LSP tools. Never emit ## user, ## assistant, ## tool output headers; markdown ## headings inside replies are fine.";
static volatile pid_t g_cp = 0;
static int g_halt = 0;
static volatile sig_atomic_t g_rst = 0;
static int g_ifd = -1;  /* inotify fd watching m.txt; -1 = no auto-refresh */
static void m_sint(int s){(void)s;if(g_cp>0)kill(g_cp,SIGTERM);}
static void m_usr1(int s){(void)s;g_rst=1;}
static void mm_w(const char *p, const char *t, const char *m);

static void m_status(const char *msg) {
    char p[P], l[256];
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    snprintf(p, P, "%s/m_status", DDIR);
    snprintf(l, 256, "[%02d:%02d:%02d] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    mm_w(p, l, "w");
    if(getenv("M_IN")&&strncmp(msg,"git ",4))fprintf(stderr,"\033[2m· %s\033[0m\n",msg);
}

static void m_commit(const char *tag) {
    if (g_halt) return;
    pid_t p = fork();
    if (p == 0) { /* intermediate: fork grandchild then exit so init adopts it (no SIGCHLD on main needed) */
        if (fork() == 0) {
            char c[B];
            snprintf(c, B,
                "{ cd '%1$s/m' && git add -A && (git diff --cached --quiet || git commit -q -m '%2$s') && git push -q 2>/dev/null "
                "&& printf '[%%s] git ✓ %2$s' \"$(date +%%H:%%M:%%S)\" > '%3$s/m_status'; } "
                "|| printf '[%%s] git ✗ %2$s' \"$(date +%%H:%%M:%%S)\" > '%3$s/m_status'",
                SDIR, tag, DDIR);
            execlp("sh", "sh", "-c", c, NULL); _exit(127);
        }
        _exit(0);
    }
    waitpid(p, NULL, 0); /* fast — intermediate exits immediately */
    char sm[32]; snprintf(sm, 32, "git sync %s", tag); m_status(sm);
}

static int mm_extract(const char *a, char *bash, size_t bsz, size_t *eo) {
    const char *o=strstr(a,"<cmd>"); if(!o) return 0;
    const char *cl=strstr(o+5,"</cmd>"); if(!cl) return -1;
    size_t bl=(size_t)(cl-o-5); if(bl>=bsz) bl=bsz-1;
    memcpy(bash,o+5,bl); bash[bl]=0;
    if(eo) *eo=(size_t)(cl-a)+6;
    return 1;
}

static int mm_delta_raw(const char *t, char *a, size_t *al, size_t sz) {
    while (*t && *t != '"' && *al + 1 < sz) {
        if (*t == '\\' && t[1]) {
            char c = t[1];
            a[(*al)++] = c == 'n' ? '\n' : c == '"' ? '"' : c == '\\' ? '\\' : c;
            t += 2;
        } else a[(*al)++] = *t++;
    }
    a[*al] = 0; return 0;
}
static int mm_delta(const char *l, char *a, size_t *al, size_t sz, int ag) {
    static const char *S[]={"\"message_stop\"","\"turn.completed\"","\"type\":\"result\""};
    static const char *F[]={"\"text_delta\",\"text\":\"","\"agent_message\"","\"role\":\"assistant\""};
    static const char *K[]={0,"\"text\":\"","\"content\":\""};
    static const int O[]={21,8,11};
    if (strstr(l,S[ag])) return 1;
    const char *t=strstr(l,F[ag]); if(!t) return 0;
    if (K[ag]) { t=strstr(t,K[ag]); if(!t) return 0; }
    return mm_delta_raw(t+O[ag],a,al,sz);
}

static int mm_stream(const char *sf, const char *sp, char *a, size_t sz, char *bash, size_t bsz) {
    a[0] = bash[0] = 0;
    int pp[2]; if (pipe(pp) < 0) return -1;
    load_cfg();
    /* env overrides allow parallel multi-provider: secondaries setenv before forking into this fn */
    const char *ag_ov=getenv("M_AGENT_OVR"), *md_ov=getenv("M_MODEL_OVR");
    const char *ag = (ag_ov&&*ag_ov)?ag_ov:cfget("m_agent"); if (!*ag) ag = "claude";
    int is_api = !strcmp(ag,"api"), is_codex = !is_api && strstr(ag,"codex")!=0, is_gemini = !is_api && strstr(ag,"gemini")!=0;
    const char *md = (md_ov&&*md_ov)?md_ov:cfget("m_model"); if (!*md) md = is_codex ? "gpt-5.5" : is_gemini ? "gemini-2.5-flash" : is_api ? "claude-opus-4-5" : "opus";
    const char *ef = cfget("m_effort"); if (!*ef) ef = is_codex ? "xhigh" : "low";
    const char *tier = cfget("m_tier");
    int has_tier = is_codex && *tier && strcmp(tier,"default");
    char errp[P]; snprintf(errp,P,"%s/m_err.log",DDIR);
    pid_t cp = fork();
    if (!cp) {
        int s = open(sf, O_RDONLY); if (s < 0) _exit(127);
        int d = open(errp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        dup2(s, 0); dup2(pp[1], 1); dup2(d, 2);
        close(s); close(d); close(pp[0]); close(pp[1]);
        if (is_api) { static const char *api_src =
"import sys,os,json,re,urllib.request as U\n"
"def emit(t):\n"
" j=json.dumps(t);print(f'data: {{\"type\":\"content_block_delta\",\"delta\":{{\"type\":\"text_delta\",\"text\":{j}}}}}');print('data: {\"type\":\"message_stop\"}');sys.exit(0)\n"
"K=os.environ.get('ANTHROPIC_API_KEY') or emit('[api] no ANTHROPIC_API_KEY env')\n"
"M=os.environ.get('M_MODEL','claude-opus-4-5')\n"
"P={'opus':(15,75),'sonnet':(3,15),'haiku':(1,5)}\n"
"ip,op=next((v for k,v in P.items() if k in M),(15,75))\n"
"T=re.sub(r'## a-loaded .*?## a-loaded-end\\n','',sys.stdin.buffer.read().decode('utf-8','replace'),flags=re.DOTALL)\n"
"S='';msgs=[]\n"
"for h,b in re.findall(r'## (system|user|assistant|tool output[^\\n]*)\\n(.*?)(?=\\n## (?:system|user|assistant|tool output)|\\Z)',T,re.DOTALL):\n"
" b=b.strip()\n"
" if not b:continue\n"
" if h=='system':S=b\n"
" elif h.startswith('tool'):\n"
"  if msgs and msgs[-1]['role']=='assistant':msgs[-1]['content']+='\\n[tool]\\n'+b\n"
" else:\n"
"  r='user' if h=='user' else 'assistant'\n"
"  if msgs and msgs[-1]['role']==r:msgs[-1]['content']+='\\n'+b\n"
"  else:msgs.append({'role':r,'content':b})\n"
"while msgs and msgs[-1]['role']=='assistant':msgs.pop()\n"
"if not msgs or msgs[-1]['role']!='user':emit('[api] no user turn')\n"
"H={'x-api-key':K,'anthropic-version':'2023-06-01','content-type':'application/json'}\n"
"def call(u,d):return U.urlopen(U.Request(u,json.dumps(d).encode(),headers=H))\n"
"B={'model':M,'system':S,'messages':msgs,'max_tokens':8192}\n"
"try:n=json.loads(call('https://api.anthropic.com/v1/messages/count_tokens',B).read())['input_tokens']\n"
"except Exception as e:emit(f'[api] count_tokens failed: {e}')\n"
"est=n*ip/1e6\n"
"try:tty=open('/dev/tty','r+')\n"
"except Exception:emit(f'[api] no tty (model={M} in={n}tok ~${est:.4f})')\n"
"tty.write(f'\\033[33m[api] {M} in={n}tok ~${est:.4f} (out @${op}/M). confirm? [y/N] \\033[0m');tty.flush()\n"
"if tty.readline().strip().lower() not in('y','yes'):emit(f'[api] aborted (was ~${est:.4f})')\n"
"B['stream']=True\n"
"try:\n"
" for ln in call('https://api.anthropic.com/v1/messages',B):\n"
"  s=ln.decode('utf-8','replace')\n"
"  if s.startswith('data: '):sys.stdout.write(s[6:]);sys.stdout.flush()\n"
"except Exception as e:emit(f'[api] stream failed: {e}')\n";
            char tf[P]; snprintf(tf,P,"%s/m_api.py",TMP); writef(tf,api_src);
            char x[B]; snprintf(x,B,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|M_MODEL='%s' python3 '%s'",md,tf);
            execlp("sh","sh","-c",x,(char*)0); }
        else if (is_gemini) { /* gemini cli phased out for antigravity; antigravity has no good single-output mode yet */
            char g[B]; snprintf(g,B,"echo 'gemini disabled — gemini cli being phased out for antigravity, which has no good single-output mode yet'>&2; exit 1");
            execlp("sh","sh","-c",g,(char*)0);
            /* original: snprintf(g,B,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|gemini -p '' --skip-trust --approval-mode plan -o stream-json -m '%s'",md); */
        }
        else if (is_codex) { char x[B],t[80]="";
            if (has_tier) snprintf(t,80," -c 'service_tier=\"%s\"'",tier);
            snprintf(x,B,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|iconv -f UTF-8 -t UTF-8 -c|codex exec --json --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox --disable shell_tool --disable unified_exec --disable tool_search --disable tool_suggest -m '%s' -c 'model_reasoning_effort=\"%s\"'%s",md,ef,t);
            execlp("sh","sh","-c",x,(char*)0);
        } else { char x[B*2];
            snprintf(x,B*2,"awk '/^## a-loaded /{s=1;next} s&&/^## a-loaded-end$/{s=0;next} !s'|claude -p --output-format stream-json --include-partial-messages --verbose --tools '' --model '%s' --effort '%s' --system-prompt \"$1\" --append-system-prompt-file <(cat %2$s/local/a_cat.txt 2>/dev/null; printf '\\n==> i.txt <==\\n'; cat %3$s/m/i.txt 2>/dev/null) --settings '{\"enabledPlugins\":{\"clangd-lsp@claude-plugins-official\":false}}'",md,ef,AROOT,SDIR);
            execlp("bash","bash","-c",x,"_",sp,(char*)0);
        }
        _exit(127);
    }
    close(pp[1]); g_cp = cp;
    const char *out_path=getenv("M_OUT_PATH"); if(!out_path||!*out_path)out_path=sf;
    int echo=!getenv("M_NOECHO");
    FILE *fp = fdopen(pp[0], "r"), *of = fopen(out_path, "a");
    off_t start = ftello(of);
    char l[32768]; size_t al = 0, pl = 0, eo;
    while (fgets(l, sizeof l, fp)) {
        int stop = mm_delta(l, a, &al, sz, is_codex?1:is_gemini?2:0);
        if (al > pl) { if (!pl) m_status("streaming"); fwrite(a + pl, 1, al - pl, of); fflush(of); if(echo)(void)!write(1, a+pl, al-pl); pl = al; }
        char *uh=strstr(a,"\n## user\n");
        if (uh || mm_extract(a, bash, bsz, &eo) > 0) { size_t cut=uh?(size_t)(uh-a):al;
            if (uh && cut<al) { a[cut]=0; al=cut; ftruncate(fileno(of),start+(off_t)cut); }
            kill(cp, SIGTERM); break; }
        if (stop) break;
    }
    fclose(fp); fclose(of); waitpid(cp, NULL, 0); g_cp = 0;
    if (!pl) { char *e=readf(errp,NULL); if(e&&*e){char m[2048]; snprintf(m,2048,"\n## error (%s)\n%s\n",ag,e); mm_w(out_path,m,"a");} free(e); }
    if (!bash[0]) mm_extract(a, bash, bsz, NULL);
    return 0;
}

static void mm_bash(const char *pty, const char *cmd, char *out, size_t sz) {
    char cn[B]; snprintf(cn, B, "%s\r", cmd);
    pid_t p = fork();
    if (!p) { execlp("tmux","tmux","send-keys","-t",pty,"-l",cn,(char*)0); _exit(127); }
    waitpid(p, NULL, 0);
    sleep(3);
    char sc[B];
    snprintf(sc, B, "tmux capture-pane -t '%s' -p -S -50|tail -30|sed 's/^/    /'", pty);
    pcmd(sc, out, sz);
}

static void mm_w(const char *p, const char *t, const char *m) {
    FILE *f = fopen(p, m); if (f) { fputs(t, f); fclose(f); }
}

static void m_winch(int s){(void)s;}
static int cmd_m_panel(int c, char **v) {
    (void)c; (void)v;
    { struct sigaction sa; sa.sa_handler=m_winch; sigemptyset(&sa.sa_mask); sa.sa_flags=0; sigaction(SIGWINCH,&sa,NULL); }
    static const char *AGTS[] = {"claude-cli","codex-cli","gemini-cli","api",NULL};
    static const char *MODS_C[] = {"opus","sonnet","haiku",NULL};
    static const char *MODS_X[] = {"gpt-5","gpt-5.5",NULL};
    static const char *MODS_G[] = {"gemini-2.5-flash","gemini-2.5-pro","gemini-3-pro-preview",NULL};
    static const char *MODS_A[] = {"claude-opus-4-5","claude-sonnet-4-6","claude-haiku-4-5",NULL};
    static const char *EFFS_C[] = {"low","medium","high","max",NULL};
    static const char *EFFS_X[] = {"low","medium","high","xhigh",NULL};
    static const char *TIERS[] = {"default","fast","flex",NULL};
    static const struct { const char *l, *cm; } OPS[] = {
        {"main agent","a m main"},{"new agent","a m new"},
        {"archive turn","a m archive turn"},{"archive","a m archive"},
        {"undo","a m archive undo"},{"restart","a m restart"},{"edit","a m edit"},{NULL,NULL}};
    struct termios old, raw; tcgetattr(0, &old); raw = old;
    raw.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    write(1, "\033[?1000h\033[?1006h", 16);
    char last[160] = "";
    int bx[64],bw[64],br[64],bi[64],sel=0; char bk[64]; int nb=0;
    for (;;) {
        load_cfg();
        const char *cg = cfget("m_agent"); if (!*cg) cg = "claude";
        int aa = !strcmp(cg,"api"), xx = !aa && strstr(cg,"codex")!=0, gg = !aa && strstr(cg,"gemini")!=0;
        const char **MODS = xx ? MODS_X : gg ? MODS_G : aa ? MODS_A : MODS_C;
        const char **EFFS = xx ? EFFS_X : EFFS_C;
        const char *cm = cfget("m_model"); if (!*cm) cm = xx ? "gpt-5.5" : gg ? "gemini-2.5-flash" : aa ? "claude-opus-4-5" : "opus";
        const char *cf = cfget("m_effort"); if (!*cf) cf = xx ? "xhigh" : "low";
        const char *ct = cfget("m_tier"); if (!*ct) ct = "default";
        int opsr = 4 + (!gg && !aa) + xx;
        struct stat st; long tot=0; char pa[P];
        static long mc=0; static time_t mt=0;
        snprintf(pa,P,"%s/m/m.txt",SDIR);
        if(!stat(pa,&st)){if(st.st_mtime!=mt){FILE*mf=fopen(pa,"r");mc=0;if(mf){char ln[4096];int s=0;while(fgets(ln,4096,mf)){if(!strncmp(ln,"## a-loaded",11))s=(ln[11]==' ');else if(!s)mc+=(long)strlen(ln);}fclose(mf);}mt=st.st_mtime;}tot+=mc;}
        snprintf(pa,P,"%s/local/a_cat.txt",AROOT); if(!stat(pa,&st)) tot+=st.st_size;
        snprintf(pa,P,"%s/m/i.txt",SDIR); if(!stat(pa,&st)) tot+=st.st_size;
        tot/=4; long lim=(gg||!strcmp(cm,"opus"))?1000000:200000; int pct=tot*100/lim;
        char hb[16]={0}; pcmd("tmux display -p -t \"$TMUX_PANE\" '#{pane_height}'",hb,16); int ph=atoi(hb);
        struct winsize ws={0}; ioctl(1,TIOCGWINSZ,&ws); int cw=ws.ws_col?ws.ws_col:80;
        if(ph<=5) sel=0;
        write(1, "\033[2J\033[H", 7); nb = 0;
        printf("\033[%dmtok %ldk/%s\033[0m ",pct>=80?31:pct>=50?33:32,tot/1000,lim>=1000000?"1M":"200k");
        if (gg) printf("gemini-cli -m %s",cm);
        else if (aa) printf("api -m %s",cm);
        else if (xx) printf("codex-cli -m %s eff=%s%s%s",cm,cf,strcmp(ct,"default")?" tier=":"",strcmp(ct,"default")?ct:"");
        else printf("claude-cli -m %s eff=%s",cm,cf);
        {char sp[P];snprintf(sp,P,"%s/m_status",DDIR);char*s=readf(sp,NULL);
         if(s){s[strcspn(s,"\n")]=0;if(*s)printf(" │ %s",s);free(s);}}
        printf("\033[K");
        {const char*lbl=ph>5?"[-] collapse $ tmux resize-pane -t \"$TMUX_PANE\" -y 2":"[+] expand $ tmux resize-pane -t \"$TMUX_PANE\" -y 15";
         printf("\033[2;1H\033[%dm%s",sel==nb?7:1,lbl); int ll=(int)strlen(lbl);
         for(int i=ll;i<cw;i++)putchar(' '); printf("\033[0m");
         bx[nb]=0;bw[nb]=cw;br[nb]=1;bk[nb]='T';bi[nb]=0;nb++;}
        if (ph > 5) { printf("\033[3;1H");
        #define VROW(lbl,arr,row,kind,curv) do{int cx=printf("%s: ",lbl);\
            for(int i=0;arr[i];i++){int s=!strcmp(curv,arr[i]),f=sel==nb,w=(int)strlen(arr[i])+2;\
                bx[nb]=cx;bw[nb]=w;br[nb]=row;bk[nb]=kind;bi[nb]=i;nb++;\
                printf("%s[%s]%s ",f?"\033[7m":s?"\033[1m":"",arr[i],f||s?"\033[0m":"");cx+=w+1;}printf("\033[K\n");}while(0)
        VROW("agent",AGTS,2,'a',cg); VROW("model",MODS,3,'m',cm);
        if (!gg && !aa) VROW("effort",EFFS,4,'e',cf);
        if (xx) VROW("tier",TIERS,5,'t',ct);
        #undef VROW
        {int cx=0;for(int i=0;OPS[i].l;i++){int f=sel==nb,w=(int)strlen(OPS[i].l)+2;
            bx[nb]=cx;bw[nb]=w;br[nb]=opsr;bk[nb]='o';bi[nb]=i;nb++;
            printf("%s[%s]%s ",f?"\033[7m":"",OPS[i].l,f?"\033[0m":"");cx+=w+1;}printf("\033[K\n");} }
        if(sel>=nb)sel=nb-1; if(sel<0)sel=0;
        char sc[B];
        if (bk[sel]=='T') snprintf(sc,B,"tmux resize-pane -t \"$TMUX_PANE\" -y %d",ph>5?2:15);
        else if (bk[sel]=='a') { const char*a=AGTS[bi[sel]]; snprintf(sc,B,"a config m_agent %s; a config m_model %s; a config m_effort %s",a,strstr(a,"codex")?"gpt-5.5":strstr(a,"gemini")?"gemini-2.5-flash":!strcmp(a,"api")?"claude-opus-4-5":"opus",strstr(a,"codex")?"xhigh":"low"); }
        else if (bk[sel]=='m') snprintf(sc,B,"a config m_model %s",MODS[bi[sel]]);
        else if (bk[sel]=='e') snprintf(sc,B,"a config m_effort %s",EFFS[bi[sel]]);
        else if (bk[sel]=='t') snprintf(sc,B,"a config m_tier %s",TIERS[bi[sel]]);
        else snprintf(sc,B,"%s",OPS[bi[sel]].cm);
        if(ph>5){int bot=opsr+2;
            printf("\033[%d;1H\033[36m$ %s\033[0m\033[K",bot,sc);
            printf("\033[%d;1H\033[90marrows select Enter run q exit %s\033[0m\033[K",bot+1,last);}
        fflush(stdout);
        char ch; ssize_t rr=read(0,&ch,1); if(rr==0)break; if(rr!=1)continue;
        if (ch == 3 || ch == 4 || ch == 'q') break;
        int hit=-1;
        if (ch == '\r' || ch == '\n' || ch == ' ') hit=sel;
        else if (ch != '\x1b') continue;
        else {
            int av; usleep(50000); ioctl(0, FIONREAD, &av); if (!av) break;
            char s[2]; if (read(0, s, 1) != 1 || s[0] != '[' || read(0, s+1, 1) != 1) continue;
            if (s[1]=='A' || s[1]=='D') { sel=(sel+nb-1)%nb; continue; }
            if (s[1]=='B' || s[1]=='C') { sel=(sel+1)%nb; continue; }
            if (s[1] != '<') continue;
            int btn=0; char mc;
            int mx=0,my=0;
            while (read(0,&mc,1)==1 && mc!=';') btn = btn*10+(mc-'0');
            while (read(0,&mc,1)==1 && mc!=';') mx = mx*10+(mc-'0');
            while (read(0,&mc,1)==1 && mc!='M' && mc!='m') my = my*10+(mc-'0');
            if (mc=='M'&&(btn==64||btn==65)) { sel=(sel+(btn==65?1:nb-1))%nb; continue; }
            if (mc!='M' || btn!=0) continue;
            int gx = mx-1, gy = my-1;
            for (int i = 0; i < nb; i++) if (gy==br[i] && gx>=bx[i] && gx<bx[i]+bw[i]) { hit=sel=i; break; }
        }
        if (hit < 0) continue;
        int i = hit;
        if (bk[i]=='T') {
            char hb2[16]={0}; pcmd("tmux display -p -t \"$TMUX_PANE\" '#{pane_height}'",hb2,16);
            char cmd[64]; snprintf(cmd,64,"tmux resize-pane -t \"$TMUX_PANE\" -y %d",atoi(hb2)>5?2:15);
            (void)!system(cmd); }
        else if (bk[i]=='a') { const char*a=AGTS[bi[i]]; cfset("m_agent",a);
            cfset("m_model",strstr(a,"codex")?"gpt-5.5":strstr(a,"gemini")?"gemini-2.5-flash":!strcmp(a,"api")?"claude-opus-4-5":"opus");
            cfset("m_effort",strstr(a,"codex")?"xhigh":"low");
            snprintf(last,160,"agent=%s",a); }
        else if (bk[i]=='m') { cfset("m_model",MODS[bi[i]]); snprintf(last,160,"model=%s",MODS[bi[i]]); }
        else if (bk[i]=='e') { cfset("m_effort",EFFS[bi[i]]); snprintf(last,160,"effort=%s",EFFS[bi[i]]); }
        else if (bk[i]=='t') { cfset("m_tier",TIERS[bi[i]]); snprintf(last,160,"tier=%s",TIERS[bi[i]]); }
        else {
            static time_t aw=0; time_t now=time(NULL);
            if (!strcmp(OPS[bi[i]].l,"archive") && now-aw>=10) {
                snprintf(last,160,"\033[33mrun [archive] again within 10s, or use [archive turn]\033[0m");
                aw=now; continue;
            }
            aw=0;
            char cmd[B],o[200]=""; snprintf(cmd,B,"%s 2>/dev/null",OPS[bi[i]].cm); int r=pcmd(cmd,o,200);
            o[strcspn(o,"\n")]=0;
            time_t tt=time(NULL); char ts[16]; strftime(ts,16,"%H:%M:%S",localtime(&tt));
            snprintf(last,160,"[%s] %s %s %s",ts,WIFEXITED(r)&&!WEXITSTATUS(r)?"\033[32mOK":"\033[31mX",OPS[bi[i]].l,o);
            if(!WEXITSTATUS(r)&&(strstr(OPS[bi[i]].l,"archive")||!strcmp(OPS[bi[i]].l,"undo"))){
                char pp[P];snprintf(pp,P,"%s/m_pid",DDIR);char *xp=readf(pp,NULL);if(xp){kill(atoi(xp),SIGUSR1);free(xp);}
            }
        }
    }
    write(1, "\033[?1000l\033[?1006l", 16);
    tcsetattr(0, TCSANOW, &old);
    return 0;
}

/* preview: first K + " … " + last K chars, NL/TAB→space. K=25, outsz>=64. */
static void m_preview(const char *s, size_t n, char *o, size_t osz) {
    const int K=25; size_t w=0;
    #define PUT(c) do{char x=(c);if(w+1<osz)o[w++]=(x=='\n'||x=='\r'||x=='\t')?' ':x;}while(0)
    if (n <= (size_t)(K*2+5)) { for(size_t i=0;i<n;i++)PUT(s[i]); }
    else { for(int i=0;i<K;i++)PUT(s[i]);
           if(w+5<osz){memcpy(o+w," … ",5);w+=5;}
           for(int i=0;i<K;i++)PUT(s[n-K+i]); }
    o[w]=0;
    #undef PUT
}

/* trim [s,e) from mp, git commit with `archive <file> <info> | <preview>`. */
static int m_arch_cut(const char *mp, char *txt, size_t tl, char *s, char *e, const char *info) {
    char prev[128]; m_preview(s, (size_t)(e-s), prev, sizeof prev);
    FILE *f=fopen(mp,"w"); if(!f) return 1;
    fwrite(txt,1,(size_t)(s-txt),f); fwrite(e,1,(size_t)((txt+tl)-e),f); fclose(f);
    char mf[P]; snprintf(mf,P,"%s/m_commit_msg",DDIR);
    FILE *mp_=fopen(mf,"w"); if(mp_){fprintf(mp_,"archive %s %s | %s\n",bname(mp),info,prev); fclose(mp_);}
    char c[B]; snprintf(c,B,"git -C '%1$s/m' add -A && git -C '%1$s/m' commit -q -F '%2$s'",SDIR,mf);
    (void)!system(c);
    printf("archived %s (%ld bytes) — a m archive hist\n",info,(long)(e-s));
    return 0;
}

static int m_archive(int c, char **v) {
    char mp[P];
    static const char *USAGE =
        "usage:\n"
        "  a m archive                       rotate m.txt (keep last user turn)\n"
        "  a m archive <file.txt>            rotate <file.txt>\n"
        "  a m archive N-M [file.txt]        archive lines N..M\n"
        "  a m archive turn                  archive last completed turn\n"
        "  a m archive hist                  list archives (git log with preview)\n"
        "  a m archive show <hash>           git show <hash> (diff of cut)\n"
        "  a m archive get <hash>            dump cut content to /tmp/m_unarch_<hash>.txt\n"
        "  a m archive undo                  revert most recent archive commit\n";
    if (c > 3 && (!strcmp(v[3],"--help")||!strcmp(v[3],"-h")||!strcmp(v[3],"help"))) { fputs(USAGE,stdout); return 0; }
    if (c > 3 && !strcmp(v[3],"hist")) { char x[B]; snprintf(x,B,"git -C '%s/m' log --oneline --grep='^archive '",SDIR); return system(x); }
    if (c > 4 && !strcmp(v[3],"show")) { char x[B]; snprintf(x,B,"git -C '%s/m' show %s",SDIR,v[4]); return system(x); }
    if (c > 4 && !strcmp(v[3],"get")) { char tf[P],x[B]; snprintf(tf,P,"/tmp/m_unarch_%s.txt",v[4]);
        snprintf(x,B,"git -C '%s/m' show %s | grep '^-[^-]' | cut -c2- > '%s' && echo %s",SDIR,v[4],tf,tf);
        return system(x); }
    if (c > 3 && (!strcmp(v[3],"undo")||!strcmp(v[3],"restore")||!strcmp(v[3],"unarchive"))) {
        if (c > 4) { char *nv[]={v[0],v[1],v[2],(char*)"get",v[4],NULL}; return m_archive(5,nv); }
        char x[B]; snprintf(x,B,"H=$(git -C '%1$s/m' log --grep='^archive ' -n 1 --format='%%H'); [ -n \"$H\" ] && git -C '%1$s/m' revert \"$H\" --no-edit || { echo 'no archive to undo'; exit 1; }",SDIR);
        return system(x);
    }
    if (c > 3 && !strcmp(v[3],"turn")) {
        const char *fn = c > 4 ? v[4] : "m.txt";
        snprintf(mp, P, "%s/m/%s", SDIR, fn);
        size_t tl; char *txt = readf(mp, &tl);
        if (!txt) { printf("not found: %s\n", mp); return 1; }
        char *last_um=NULL, *prev_um=NULL, *p=txt;
        char *aend = strstr(txt, "## a-loaded-end\n");
        while ((p = strstr(p+1, "\n## user\n"))) {
            if (aend && p+1 < aend) continue;
            prev_um = last_um; last_um = p + 1;
        }
        if (!prev_um) { puts("no completed turn to archive"); free(txt); return 0; }
        int r = m_arch_cut(mp, txt, tl, prev_um, last_um, "turn"); free(txt); return r;
    }
    if (c == 3 || (c == 4 && strstr(v[3], ".txt"))) {
        const char *fn = (c == 4) ? v[3] : "m.txt";
        snprintf(mp, P, "%s/m/%s", SDIR, fn);
        size_t tl; char *txt = readf(mp, &tl);
        if (!txt) { printf("file not found: %s\n", mp); return 1; }
        char *first=NULL, *aend=strstr(txt, "## a-loaded-end\n");
        if (aend) first = strstr(aend, "\n## user\n");
        if (!first) first = strstr(txt, "\n## user\n");
        if (!first) { puts("no conversation"); free(txt); return 0; }
        first++;
        char *last = first, *p = first;
        while ((p = strstr(p + 1, "\n## user\n")) != NULL) last = p + 1;
        if (last == first) { puts("nothing to rotate"); free(txt); return 0; }
        int r = m_arch_cut(mp, txt, tl, first, last, "rotate"); free(txt); return r;
    }
    if (!isdigit((unsigned char)v[3][0])) { fprintf(stderr,"unknown: %s\n",v[3]); fputs(USAGE,stderr); return 1; }
    int N = atoi(v[3]); const char *dash = strchr(v[3], '-');
    int M = dash ? atoi(dash + 1) : N;
    if (N < 1 || M < N) { fprintf(stderr, "invalid range '%s'\n", v[3]); return 1; }
    const char *fn = c > 4 ? v[4] : "m.txt";
    snprintf(mp, P, "%s/m/%s", SDIR, fn);
    size_t tl; char *txt = readf(mp, &tl);
    if (!txt) { printf("file not found: %s\n", mp); return 1; }
    char *s = txt; int ln = 1;
    while (ln < N && s < txt + tl) { if (*s++ == '\n') ln++; }
    if (ln < N) { printf("past EOF (%d lines)\n", ln); free(txt); return 1; }
    char *e = s; int cnt = 0;
    while (e < txt + tl && cnt < M - N + 1) { if (*e++ == '\n') cnt++; }
    char info[32]; snprintf(info,32,"L%d-%d",N,M);
    int r = m_arch_cut(mp, txt, tl, s, e, info); free(txt); return r;
}

static int m_reinit(const char *fn) {
    char c[P]; snprintf(c,P,"%s/m_file",DDIR); mm_w(c,fn,"w");
    snprintf(c,P,"%s/m_pid",DDIR); char *p=readf(c,NULL); if(p) kill(atoi(p),SIGUSR1); free(p); return 0;
}
static int m_restart(void){CWD(cd);char c[B];snprintf(c,B,"tmux respawn-window -k -c '%s' 'a m'",cd);(void)!system(c);return 0;}
static int m_main(void){return m_reinit("m.txt");}
static int m_new(void){char ad[P];snprintf(ad,P,"%s/m/agent",SDIR);mkdirp(ad);time_t t=time(NULL);char fn[64];strftime(fn,64,"agent/%Y-%m-%d_%H-%M-%S.txt",localtime(&t));return m_reinit(fn);}

static void m_set(const char *k,const char *v){char ck[32];snprintf(ck,32,"m_%s",k);cfset(ck,v);
  if(!strcmp(k,"agent")){cfset("m_model",strstr(v,"codex")?"gpt-5.5":strstr(v,"gemini")?"gemini-2.5-flash":!strcmp(v,"api")?"claude-opus-4-5":"opus");cfset("m_effort",strstr(v,"codex")?"xhigh":"low");}}
/* live-filter picker: anchors menu at bottom of pane via ABSOLUTE row positioning.
 * \033[s/\033[u proved unreliable in tmux across scrolls — each arrow rendered below
 * the previous, leaving stacked menus. Now we jump to a known absolute row each redraw. */
static int m_pick(const char *cat,const char *const *items,int n,char *out,size_t osz){
    struct winsize ws; ioctl(1,TIOCGWINSZ,&ws); int rows=ws.ws_row?ws.ws_row:24;
    int rsv=n+2; if(rsv>rows-1)rsv=rows-1; if(rsv>20)rsv=20;
    int top=rows-rsv+1;
    #define CLR() printf("\033[%d;1H\033[J",top)
    char f[48]=""; int fl=0,sel=0;
    for(;;){
        int fm[64],nf=0;
        for(int i=0;i<n&&nf<64;i++) if(!fl||strcasestr(items[i],f)) fm[nf++]=i;
        if(sel>=nf)sel=nf?nf-1:0; if(sel<0)sel=0;
        CLR();
        printf("\033[36m%s:\033[0m %s",cat,f);                                 /* filter row, no trailing \n */
        for(int i=0;i<nf&&i<rsv-1;i++){
            const char *it=items[fm[i]]; const char *t=strchr(it,'\t');
            int cl=t?(int)(t-it):(int)strlen(it);
            printf("\n  %s%.*s%s",i==sel?"\033[7m> ":"  ",cl,it,i==sel?"\033[0m":"");
            if(t)printf("  \033[90m%s\033[0m",t+1);                              /* desc in grey */
        }
        printf("\033[%d;%dH",top,(int)strlen(cat)+3+fl); fflush(stdout);       /* cursor back to filter, no scroll */
        unsigned char c; if(read(0,&c,1)!=1){CLR();return -1;}
        if(c==27){int av; usleep(50000); ioctl(0,FIONREAD,&av);
            if(av>=2){char s[2]; (void)!read(0,s,2);
                if(s[0]=='['||s[0]=='O'){
                    if(s[1]=='A'){if(sel>0)sel--;continue;}
                    if(s[1]=='B'){sel++;continue;}
                }}
            CLR();return -1;}
        if(c==3){CLR();return -1;}
        if(c==9){CLR();return 0;}
        if(c==21){f[0]=0;fl=0;sel=0;continue;}
        if(c=='\r'||c=='\n'){if(!nf)continue; CLR(); snprintf(out,osz,"%s",items[fm[sel]]); return 1;}
        if(c==127||c==8||c==0xff){if(fl){f[--fl]=0; sel=0;} else {CLR(); return -1;}}
        else if(c>=' '&&c<127&&fl<47){f[fl++]=(char)c;f[fl]=0;sel=0;}
    }
    #undef CLR
}
static void m_menu(void);
/* raw-mode line reader: handles BSpace on every char (incl first), bracketed paste, '/' as instant menu hotkey. */
static size_t m_read_line(char *buf,size_t sz){
    struct termios o,r; tcgetattr(0,&o); r=o;
    r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG); r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&r);
    (void)!write(1,"\033[?2004h",8);
    size_t bl=0; int paste=0;
    while(bl<sz-1){
        unsigned char c;
        struct pollfd pfd[2]={{0,POLLIN,0},{g_ifd,POLLIN,0}};
        int nfd=g_ifd>=0?2:1;
        int pr=poll(pfd,(nfds_t)nfd,-1);
        if(pr<0)break;  /* signal interrupt; cmd_m checks g_rst */
        if(nfd>1&&(pfd[1].revents&POLLIN)){
            char ib[4096]; while(read(g_ifd,ib,sizeof ib)>0){} /* drain events */
            bl=0; break;  /* file changed → caller re-renders */
        }
        if(read(0,&c,1)!=1)break;
        if(bl==0&&!paste&&c=='/'){m_menu();break;}
        if(c=='\r'||c=='\n'){if(paste){buf[bl++]='\n';write(1,"\n",1);continue;} write(1,"\n",1); break;}
        if(!paste&&(c==127||c==8||c==0xff)){if(bl){bl--; write(1,"\b \b",3);} continue;}
        if(!paste&&c==3){bl=0; write(1,"^C\n",3); break;}
        if(c==27){int av;usleep(20000);ioctl(0,FIONREAD,&av);
            if(av>0){char s[8]; int rn=(int)read(0,s,(size_t)(av>8?8:av));
                if(rn>=5){if(!memcmp(s,"[200~",5)){paste=1;continue;} if(!memcmp(s,"[201~",5)){paste=0;continue;}}}
            continue;}
        if(c>=32){buf[bl++]=(char)c; if(!paste)write(1,&c,1);}
    }
    buf[bl]=0;
    (void)!write(1,"\033[?2004l",8);
    tcsetattr(0,TCSANOW,&o);
    return bl;
}
/* one pick → run via `a m <pick>`. Returns 1 if picked, 0 if cancelled. */
static int m_step(const char *cat,const char *const *items,int n,char *out,size_t osz){
    struct termios o,r; tcgetattr(0,&o); r=o; r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG); r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&r); out[0]=0;
    int rc=m_pick(cat,items,n,out,osz);
    tcsetattr(0,TCSANOW,&o);
    if(rc>0){char *t=strchr(out,'\t'); if(t)*t=0; char shc[256]; snprintf(shc,256,"a m %s 2>&1",out); (void)!system(shc); return 1;}
    return 0;
}
/* Flat menu of all m subcommands. Picking 'agent X' drops into nested model→effort→(codex)tier
 * flow with agent-narrowed options; picking anything else runs as a one-shot. */
static void m_menu(void){
    /* "cmd\tdesc" — desc shown in grey by m_pick, stripped by m_step before exec.
     * gemini cli commented out: being phased out for antigravity, no single-output mode yet. */
    static const char *const ops[]={
        "new\tnew chat, fresh file",
        "import\timport latest claude code chat",
        "main\topen main m.txt",
        "restart\trespawn this window",
        "edit\topen file in e",
        "panel\trich panel UI",
        "reset\tclear bash pty",
        "set\t★ MULTI: clear → single",
        "set claude:opus codex:gpt-5.5\t★ MULTI: opus + gpt-5.5",
        "set claude:haiku codex:gpt-5.5\t★ MULTI: haiku + gpt-5.5",
        "set claude:opus claude:sonnet\t★ MULTI: opus + sonnet",
        "agent claude\tswitch single → claude",
        "agent codex\tswitch single → codex",
        "agent api\tswitch single → api",
        "model opus\tclaude opus (smartest)",
        "model sonnet\tclaude sonnet (balanced)",
        "model haiku\tclaude haiku (fastest)",
        "model gpt-5\tcodex gpt-5",
        "model gpt-5.5\tcodex gpt-5.5",
        "model claude-opus-4-5\tapi claude opus",
        "model claude-sonnet-4-6\tapi claude sonnet",
        "model claude-haiku-4-5\tapi claude haiku",
        "effort low\tfast cheap",
        "effort medium\tbalanced",
        "effort high\tdeeper",
        "effort max\tmax (claude)",
        "effort xhigh\tmax (codex)",
        "tier default\tdefault speed",
        "tier fast\tpriority lane",
        "tier flex\tcheap slow",
        "archive\trotate full conversation",
        "archive turn\ttrim last turn",
        "archive undo\trevert archive",
        "archive hist\tlist archives"};
    char v[128];
    if(!m_step("cmd",ops,(int)(sizeof ops/sizeof *ops),v,sizeof v))return;
    if(strncmp(v,"agent ",6))return;
    const char *ag=v+6; int cdx=strstr(ag,"codex")!=0,gmn=strstr(ag,"gemini")!=0,api=!strcmp(ag,"api");
    static const char *const M_CL[]={"model opus","model sonnet","model haiku"};
    static const char *const M_CX[]={"model gpt-5","model gpt-5.5"};
    static const char *const M_GM[]={"model gemini-2.5-flash","model gemini-2.5-pro","model gemini-3-pro-preview"};
    static const char *const M_AP[]={"model claude-opus-4-5","model claude-sonnet-4-6","model claude-haiku-4-5"};
    if(!m_step("model",cdx?M_CX:gmn?M_GM:api?M_AP:M_CL,cdx?2:3,v,sizeof v))return;
    static const char *const E_CL[]={"effort low","effort medium","effort high","effort max"};
    static const char *const E_CX[]={"effort low","effort medium","effort high","effort xhigh"};
    if(!m_step("effort",cdx?E_CX:E_CL,4,v,sizeof v))return;
    if(cdx){static const char *const T[]={"tier default","tier fast","tier flex"}; m_step("tier",T,3,v,sizeof v);}
}
/* convert one Claude Code .jsonl → fresh agent/<ts>-cc.txt, switch m_file, signal m */
static int m_import_run(const char *src) {
    static const char *py =
"import json,sys,os\n"
"src=sys.argv[1]\n"
"sys.stdout.write('## system\\nimported from claude code: '+src+'\\n')\n"
"for ln in open(src):\n"
" try:e=json.loads(ln)\n"
" except:continue\n"
" m=e.get('message') or {};r=m.get('role');co=m.get('content')\n"
" if r not in('user','assistant'):continue\n"
" t=''\n"
" if isinstance(co,str):t=co\n"
" elif isinstance(co,list):t='\\n'.join(b.get('text','')for b in co if isinstance(b,dict)and b.get('type')=='text')\n"
" if t.strip():sys.stdout.write('## %s\\n%s\\n'%(r,t))\n"
"sys.stdout.write('## user\\n')\n";
    char tf[P]; snprintf(tf,P,"%s/m_imp.py",TMP); writef(tf,py);
    time_t t=time(NULL); char fn[64]; strftime(fn,64,"agent/%Y%m%d-%H%M%S-cc.txt",localtime(&t));
    char outp[P]; snprintf(outp,P,"%s/m/%s",SDIR,fn);
    char ad[P]; snprintf(ad,P,"%s/m/agent",SDIR); mkdirp(ad);
    char cmd[B]; snprintf(cmd,B,"python3 '%s' '%s' > '%s'",tf,src,outp);
    if(system(cmd)){fprintf(stderr,"import failed\n"); return 1;}
    char pf[P]; snprintf(pf,P,"%s/m_file",DDIR); writef(pf,fn);
    printf("imported → %s (a m to open)\n",fn);
    snprintf(cmd,B,"(cd '%s/m'&&git add -A&&git commit -qm 'import %s'&&git push -q 2>/dev/null)&",SDIR,fn);(void)!system(cmd);
    snprintf(pf,P,"%s/m_pid",DDIR); char *p=readf(pf,NULL); if(p&&*p)kill(atoi(p),SIGUSR1); free(p);
    return 0;
}
/* a m import → nested picker: source (claude) → conversation list → import */
static int m_import(int c, char **v) {
    perf_disarm(); /* interactive picker blocks on user input — disable the 1s subcmd perf timer */
    if(c > 3) return m_import_run(v[3]);
    (void)!write(1,"\033[2J\033[H",7);
    static const char *const sources[] = {"claude code\tlocal CC transcripts"};
    int nsrc = (int)(sizeof sources/sizeof *sources);
    char src_pick[64];
    { struct termios o,r; tcgetattr(0,&o); r=o;
      r.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG); r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
      tcsetattr(0,TCSANOW,&r);
      int rc = m_pick("import source",sources,nsrc,src_pick,sizeof src_pick);
      tcsetattr(0,TCSANOW,&o);
      if(rc <= 0) return 0;
      char *t=strchr(src_pick,'\t'); if(t)*t=0; }
    (void)!write(1,"\033[2J\033[H",7);
    static const char *py_ls =
"import json,glob,os,time,re\n"
"for p in sorted(glob.glob(os.path.expanduser('~/.claude/projects/*/*.jsonl')),key=os.path.getmtime,reverse=True)[:30]:\n"
" s=''\n"
" try:\n"
"  for ln in open(p):\n"
"   try:e=json.loads(ln)\n"
"   except:continue\n"
"   m=e.get('message')or{}\n"
"   if m.get('role')!='user':continue\n"
"   co=m.get('content','')\n"
"   t=co if isinstance(co,str) else next((b.get('text','')for b in co if isinstance(b,dict)and b.get('type')=='text'),'')\n"
"   s=re.split(r'\\n## user\\n',t)[-1].strip()\n"
"   if s and not s.startswith('<'):break\n"
" except:pass\n"
" mt=time.strftime('%m-%d %H:%M',time.localtime(os.path.getmtime(p)))\n"
" print(f'{mt} {os.path.basename(p)[:8]} · {s.replace(chr(10),chr(32))[:60]}\\t{p}')\n";
    char tf[P]; snprintf(tf,P,"%s/m_ls.py",TMP); writef(tf,py_ls);
    char cmd[B]; snprintf(cmd,B,"python3 '%s'",tf);
    static char buf[32768]; buf[0]=0; pcmd(cmd,buf,sizeof buf);
    static char items_buf[32][384];
    const char *items_ptr[32]; int n=0;
    for(char *p=buf; *p && n<32; ){
        char *nl=strchr(p,'\n'); if(nl)*nl=0;
        if(*p){snprintf(items_buf[n],384,"%s",p); items_ptr[n]=items_buf[n]; n++;}
        if(!nl) break; p=nl+1;
    }
    if(n==0){fprintf(stderr,"no claude code conversations found\n"); return 1;}
    struct termios o,r; tcgetattr(0,&o); r=o;
    r.c_lflag &= ~(tcflag_t)(ICANON|ECHO|ISIG); r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&r);
    char pick[384];
    int rc = m_pick("conversation",items_ptr,n,pick,sizeof pick);
    tcsetattr(0,TCSANOW,&o);
    if(rc <= 0) return 0;
    char *tab = strchr(pick,'\t'); if(!tab){fprintf(stderr,"bad pick\n"); return 1;}
    return m_import_run(tab+1);
}
static int m_reset(void) {
    char pf[P],c[B]; snprintf(pf,P,"%s/m_pty",DDIR);
    char *p=readf(pf,NULL); if(!p||!*p){puts("no pty (a m not running here)");free(p);return 1;}
    p[strcspn(p,"\n")]=0;
    snprintf(c,B,"tmux send-keys -t '%1$s' C-c; tmux send-keys -t '%1$s' 'clear' Enter",p);
    int r=system(c); free(p); return r;
}

static int cmd_m(int c, char **v) {
    if (c>2&&!strcmp(v[2],"edit")) { char p[P],fn[64]="m.txt",*f; const char*me=getenv("M_FILE");
        if(me&&*me)snprintf(fn,64,"%s",me);
        else{snprintf(p,P,"%s/m_file",DDIR); f=readf(p,NULL); if(f&&*f){f[strcspn(f,"\n")]=0;snprintf(fn,64,"%s",f);} free(f);}
        char cc[B]; snprintf(cc,B,"tmux set -w pane-scrollbars on;tmux split-window -fh -l 80%% -c '%s/m' 'exec e --nosb --nofold --tail %s'",SDIR,fn); return system(cc); }
    if (c > 2 && !strcmp(v[2], "archive")) return m_archive(c, v);
    if (c > 2 && (!strcmp(v[2],"model")||!strcmp(v[2],"agent")||!strcmp(v[2],"effort")||!strcmp(v[2],"tier")||!strcmp(v[2],"set"))) {
        char val[256]=""; if(c>3)ajoin(val,256,c,v,3); load_cfg(); m_set(v[2],val); return 0;}
    if (c > 2 && !strcmp(v[2], "panel")) return cmd_m_panel(c, v);
    if (c > 2 && !strcmp(v[2], "p")) { char pf[P]; snprintf(pf,P,"%s/m_panel",DDIR); char*p=readf(pf,NULL);
        if(p){p[strcspn(p,"\n")]=0; char hc[128],hb[16]={0}; snprintf(hc,128,"tmux display -p -t %s '#{pane_height}'",p); pcmd(hc,hb,16);
            char cmd[128]; snprintf(cmd,128,"tmux resize-pane -t %s -y %d",p,atoi(hb)>5?2:15); (void)!system(cmd); free(p);} return 0; }
    if (c > 2 && !strcmp(v[2], "reset")) return m_reset();
    if (c > 2 && !strcmp(v[2], "restart")) return m_restart();
    if (c > 2 && !strcmp(v[2], "main")) return m_main();
    if (c > 2 && !strcmp(v[2], "new")) return m_new();
    if (c > 2 && !strcmp(v[2], "import")) return m_import(c, v);
    if (getenv("M_IN")) { puts("already in a m (nested chat blocked) — use: a m archive | panel | reset"); return 1; }
    perf_disarm(); /* interactive: subcommands above stay perf-armed; the chat loop runs unbounded */
    /* (existing-instance switch removed — flaky when pid lives but window died; just open fresh) */
    char b[B], sf[P], pty[64] = "";
    CWD(w); struct tm*tt=localtime(&(time_t){time(NULL)}); char sn[64];
    snprintf(sn,64,"m-%s-%02d%02d%02d",bname(w),tt->tm_hour,tt->tm_min,tt->tm_sec);
    if (!getenv("TMUX")) { ajoin(b,B,c,v,0); tm_new(sn,w,b); tm_go(sn); return 0; }
    signal(SIGINT, m_sint); /* m_commit double-forks → init reaps; secondary waitpid stays reliable */
    { struct sigaction sa; sa.sa_handler=m_usr1; sigemptyset(&sa.sa_mask); sa.sa_flags=0; sigaction(SIGUSR1,&sa,NULL); }
    { char pb[16]; snprintf(b,B,"%s/m_pid",DDIR); snprintf(pb,16,"%d",getpid()); mm_w(b,pb,"w"); }
    char fnb[128]="m.txt";const char *fn=c>2?v[2]:fnb;
    char pf[P]; snprintf(pf,P,"%s/m_file",DDIR);
    if(c<=2){char*sp=readf(pf,NULL);if(sp&&*sp){sp[strcspn(sp,"\n")]=0;snprintf(fnb,128,"%s",sp);}free(sp);}
    snprintf(sf, P, "%s/m/%s", SDIR, fn);
    mm_w(pf,fn,"w");
    setenv("M_IN", "1", 1);
    /* SYNC clone only on first ever launch (no .git yet). Pull is deferred to first-render block (async). */
    { char gp[P]; snprintf(gp,P,"%s/m/.git",SDIR);
      if(!dexists(gp)){char ic[B]; snprintf(ic,B,"cd '%1$s'&&(U=$(gh repo view m --json url --jq .url 2>/dev/null) && git clone \"$U.git\" m 2>/dev/null || gh repo create m --private --clone)",SDIR); (void)!system(ic);} }
#ifdef __linux__
    g_ifd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    if(g_ifd>=0) inotify_add_watch(g_ifd, sf, IN_CLOSE_WRITE);
#endif
    int _first=1;
    for (;;) {
        g_halt = 0;
        {char *sp=readf(pf,NULL); if(sp&&*sp){sp[strcspn(sp,"\n")]=0;
          if(strcmp(sp,fn)){snprintf(fnb,128,"%s",sp); fn=fnb; snprintf(sf,P,"%s/m/%s",SDIR,fn);
#ifdef __linux__
            if(g_ifd>=0) inotify_add_watch(g_ifd,sf,IN_CLOSE_WRITE);
#endif
          }} free(sp);}
        /* render: tail of m.txt sized to terminal rows. Sysprompt is seeded ONLY into empty/new files
         * (sz==0) — never modifies existing content, so a backgrounded git pull can fast-forward without
         * fighting a local rewrite. Drifted sysprompt headers are preferred over data loss. */
        { static char tb[16384]; size_t n=0;
          struct stat st; FILE *rf=fopen(sf,"r"); off_t sz=0;
          if(rf){fstat(fileno(rf),&st); sz=st.st_size; if(sz>16384)fseek(rf,sz-16384,SEEK_SET); n=fread(tb,1,sizeof tb,rf); fclose(rf);}
          struct winsize ws={0}; ioctl(1,TIOCGWINSZ,&ws); int rows=ws.ws_row?ws.ws_row-2:22;
          char *o=tb+n; int nl=0; while(o>tb&&nl<rows){if(*--o=='\n')nl++;} if(o>tb)o++;
          (void)!write(1,"\033[2J\033[H",7); (void)!write(1,o,(size_t)(tb+n-o));
          if(sz==0){FILE *f=fopen(sf,"w"); if(f){fprintf(f,"## system\n%s\n## user\n",M_SYS); fclose(f);}} }
        { char sp[P]; snprintf(sp,P,"%s/m_status",DDIR); char *s=readf(sp,NULL);
          if(s&&*s){s[strcspn(s,"\n")]=0; printf("\n[%s]",s);} free(s); }
        load_cfg();
        { const char *ms=cfget("m_set");
          if(ms&&*ms){
            printf("\n\033[1m%s\033[0m  \033[1;35m★MULTI\033[0m \033[35m%s\033[0m  \033[90m/=menu\033[0m",fn,ms);
          } else {
            const char *ag=cfget("m_agent"),*md=cfget("m_model"),*ef=cfget("m_effort"),*ti=cfget("m_tier");
            printf("\n\033[1m%s\033[0m  \033[36msingle %s·%s·%s%s%s\033[0m  \033[90m/=menu (★set… for multi)\033[0m",fn,*ag?ag:"claude",*md?md:"opus",*ef?ef:"low",
                   *ti&&strcmp(ti,"default")?"·":"",*ti&&strcmp(ti,"default")?ti:"");
          } fflush(stdout); }
        write(1,"\n> ",3);
        if(_first){_first=0; tm_rename(sn);
          /* async pull: inotify fires re-render when new content lands. */
          snprintf(b,B,"(cd %s/m&&git pull --rebase --autostash -q 2>/dev/null)&",SDIR);(void)!system(b); }
        /* chat input — '/' inside m_read_line opens menu directly, no peek-then-pass-back seam */
        static char m[65536]; size_t ml=m_read_line(m,sizeof m);
        if(g_rst){g_rst=0;continue;}
        if(!ml)continue;
        m[ml]='\n';m[ml+1]=0;mm_w(sf, m, "a"); m_commit("u");
        for (int i = 0; i < 10; i++) {
            m_status("thinking");
            mm_w(sf, "\n## assistant\n", "a");
            (void)!write(1,"\n## assistant\n",14);
            static char a[64*1024], bash[8*1024], ob[16*1024];
            /* multi-provider: m_set "ag:md ag:md …", first=primary streams, rest=secondaries fork in parallel */
            struct {char ag[32],md[32],tmp[P]; pid_t pid;} sec[8]; int nsec=0;
            char prim_ag[32]="",prim_md[32]="";
            { const char *ms=cfget("m_set"); if(ms&&*ms){char buf[256]; snprintf(buf,256,"%s",ms);
                char *t=strtok(buf," "); int ix=0;
                while(t){char *cl=strchr(t,':'); if(cl){*cl=0;
                    if(ix==0){snprintf(prim_ag,32,"%s",t);snprintf(prim_md,32,"%s",cl+1);}
                    else if(nsec<8){snprintf(sec[nsec].ag,32,"%s",t);snprintf(sec[nsec].md,32,"%s",cl+1);
                        snprintf(sec[nsec].tmp,P,"%s/m_sec_%d_%d.txt",DDIR,(int)getpid(),nsec); nsec++;}
                    ix++;} t=strtok(NULL," ");} } }
            for(int s=0;s<nsec;s++){unlink(sec[s].tmp); pid_t w=fork();
                if(w==0){setenv("M_AGENT_OVR",sec[s].ag,1); setenv("M_MODEL_OVR",sec[s].md,1);
                    setenv("M_OUT_PATH",sec[s].tmp,1); setenv("M_NOECHO","1",1);
                    static char a2[64*1024],b2[8*1024]; mm_stream(sf,M_SYS,a2,sizeof a2,b2,sizeof b2); _exit(0);}
                sec[s].pid=w;}
            if(prim_ag[0]){setenv("M_AGENT_OVR",prim_ag,1);setenv("M_MODEL_OVR",prim_md,1);}
            else{unsetenv("M_AGENT_OVR");unsetenv("M_MODEL_OVR");}
            unsetenv("M_OUT_PATH"); unsetenv("M_NOECHO");
            if (mm_stream(sf, M_SYS, a, sizeof a, bash, sizeof bash) < 0) break;
            if(nsec){m_status("waiting on secondaries");
                for(int s=0;s<nsec;s++){waitpid(sec[s].pid,NULL,0);
                    char *sc=readf(sec[s].tmp,NULL);
                    if(sc&&*sc){char hdr[64]; snprintf(hdr,64,"\n## assistant [%s·%s]\n",sec[s].ag,sec[s].md);
                        mm_w(sf,hdr,"a"); mm_w(sf,sc,"a"); free(sc);}
                    unlink(sec[s].tmp);}}
            m_commit("a"); if (g_halt) break;
            if (!bash[0]) break;
            /* Lazy bash pty: pay tmux split cost once, only when a <cmd> actually runs. */
            if (!pty[0]) { char sc[B];
              snprintf(sc,B,"tmux split-window -t $TMUX_PANE -e M_IN=1 -e M_FILE=%s -dv -l 7 -P -F '#{pane_id}' 'cd %s/m;exec bash'",fn,SDIR);
              pcmd(sc,pty,64); pty[strcspn(pty,"\n")]=0;
              snprintf(pf,P,"%s/m_pty",DDIR); mm_w(pf,pty,"w"); }
            m_status("running");
            mm_bash(pty, bash, ob, sizeof ob);
            char tb[20000]; time_t t = time(NULL);
            size_t n = strftime(tb, 64, "\n## tool output %FT%T\n", localtime(&t));
            snprintf(tb + n, sizeof tb - n, "%s\n", ob);
            mm_w(sf, tb, "a"); (void)!write(1,tb,strlen(tb));
            m_commit("t"); if (g_halt) break;
        }
        mm_w(sf, "\n## user\n", "a");
    }
    return 0;
}
