/* utilities */
#ifdef __ANDROID__
#define OPENER "termux-open"
#define APP_CMD "CLASSPATH=/data/data/com.termux/files/usr/libexec/termux-am/am.apk /system/bin/app_process / com.termux.termuxam.Am start --user 0 -n"
#elif defined(__APPLE__)
#define OPENER "open"
#define APP_CMD "open -a"
#elif defined(__CYGWIN__)
#define OPENER "cygstart"
#define APP_CMD "cygstart"
#else
#define OPENER "xdg-open"
#define APP_CMD "gtk-launch"
#endif
static void bg_exec(const char *c,const char *a){if(!fork()){setsid();int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(n,1);dup2(n,2);close(n);execlp(c,c,a,(char*)NULL);_exit(1);}}
static int fexists(const char *p) { struct stat s; return stat(p, &s) == 0; }
static int dexists(const char *p) { struct stat s; return stat(p, &s) == 0 && S_ISDIR(s.st_mode); }
static void mkdirp(const char *p) { char t[P]; snprintf(t,P,"%s",p); for(char*s=t+1;*s;s++) if(*s=='/'){*s=0;mkdir(t,0755);*s='/';} mkdir(t,0755); }

static struct stat rfs;
static char *readf(const char *p, size_t *len) {
#ifdef __CYGWIN__  /* files in DDIR = the exe's dir: kernel32 read ~0.1ms vs cygwin open ~0.3ms, no path conversion */
    static char w[P];char e[P];size_t dl=strlen(DDIR);ssize_t k;long long z,t;DWORD r;HANDLE h;
    if(!*w){k=readlink("/proc/self/exe",e,P-1);*w=1;if(k>(ssize_t)dl&&!strncmp(e,DDIR,dl)&&e[dl]=='/'&&!memchr(e+dl+1,'/',(size_t)k-dl-1)&&GetModuleFileNameA(0,w,P))*strrchr(w,'\\')=0;}
    if(*w>1&&!strncmp(p,DDIR,dl)&&p[dl]=='/'){char*b=0;snprintf(e,P,"%s\\%s",w,p+dl+1);
        if((h=CreateFileA(e,1u<<31,7,0,3,0,0))!=(HANDLE)-1){if(GetFileSizeEx(h,(void*)&z)&&GetFileTime(h,0,0,(void*)&t)&&(b=malloc((size_t)z+1))&&ReadFile(h,b,(DWORD)z,&r,0)){b[r]=0;if(len)*len=r;rfs.st_mtime=(time_t)(t/10000000-11644473600LL);}else free(b),b=0;
            CloseHandle(h);if(b)return b;}}
#endif
    int fd = open(p, O_RDONLY); if (fd < 0) return NULL;
    if (fstat(fd, &rfs) < 0) { close(fd); return NULL; }
    size_t sz = (size_t)rfs.st_size;
    char *b = malloc(sz + 1); if (!b) { close(fd); return NULL; }
    ssize_t n = read(fd, b, sz); close(fd);
    if (n < 0) { free(b); return NULL; }
    b[n] = 0; if (len) *len = (size_t)n; return b;
}

static int catf(const char *p) {
    int fd = open(p, O_RDONLY); if (fd < 0) return -1;
    char b[8192]; ssize_t n;
    while ((n = read(fd, b, sizeof(b))) > 0) (void)!write(STDOUT_FILENO, b, (size_t)n);
    close(fd); return 0;
}

static void writef(const char *p, const char *data) {
    FILE *f = fopen(p, "w"); if (f) { fputs(data, f); fclose(f); }
}

/* commit-state path per cwd+pane (A_PANE override for the review pane) */
static void commit_path(char*o){CWD(w);int n=snprintf(o,P,"%s/commit_",DDIR);for(char*p=w;*p&&n<P-1;p++)o[n++]=*p=='/'?'_':*p;
    const char*tp=getenv("A_PANE");if(!tp||!*tp)tp=getenv("TMUX_PANE");
    if(tp&&*tp&&n<P-1){o[n++]='_';for(const char*q=tp;*q&&n<P-1;q++)o[n++]=isalnum((unsigned char)*q)?*q:'_';}o[n]=0;}

static int pcmd(const char *cmd, char *out, int sz) {
    if (out) out[0] = 0;
    FILE *f = popen(cmd, "r"); if (!f) return -1;
    if (out) { int n = 0; char b[B];
        while (fgets(b, B, f) && n + (int)strlen(b) < sz - 1) n += sprintf(out + n, "%s", b);
    } else { char b[B]; while (fgets(b, B, f)) ; }
    return pclose(f);
}


static const char *bname(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }


static int ajoin(char*b,int sz,int argc,char**argv,int from){int l=0;for(int i=from;i<argc;i++)l+=snprintf(b+l,(size_t)(sz-l),"%s%s",i>from?" ":"",argv[i]);return l;}

/* rapid input loop: empty/ESC/ctrl-c exits; bracketed paste = ONE note, any length, echoed count+tail */
static void rapid(const char *prompt, void (*fn)(const char*)) {
    if (!isatty(STDIN_FILENO)) return; perf_disarm();
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSAFLUSH,&r);
    (void)!write(1,"\x1b[?2004h",8);
    static size_t cap;static char *b;if(!b){cap=65536;b=malloc(cap);}
    #define RFIT do{if(n+2>cap){cap*=2;b=realloc(b,cap);}}while(0)
    int quit=0;unsigned char c;
    while(!quit){fputs(prompt,stdout);fflush(stdout);size_t n=0;
        for(;;){if(read(0,&c,1)!=1||c==3){quit=1;break;}                  
            if(c=='\n'||c=='\r')break;
            if(c==127||c==8){if(n){n--;(void)!write(1,"\b \b",3);}continue;} 
            if(c==27){struct pollfd p={0,POLLIN,0};                       /* ESC: lone=quit, else CSI seq */
                if(poll(&p,1,40)<=0){quit=1;break;}
                char s[8];int sl=0;(void)!read(0,&c,1);                   
                while(read(0,&c,1)==1){if(sl<7)s[sl++]=(char)c;if(c>=64&&c<127)break;}s[sl]=0;
                if(!strcmp(s,"200~")){                                    /* bracketed paste = one note */
                    while(read(0,&c,1)==1){
                        if(c==27){(void)!read(0,&c,1);while(read(0,&c,1)==1&&!(c>=64&&c<127)){}break;} 
                        RFIT;b[n++]=(char)c;}
                    {size_t ts=n>60?n-60:0;while(ts<n&&(b[ts]&0xC0)==0x80)ts++;char tl[64];size_t j=0;
                     for(size_t k=ts;k<n;k++)tl[j++]=(char)(b[k]=='\n'||b[k]=='\t'?' ':b[k]);tl[j]=0;
                     if(ts)printf("\033[2m%zuc…\033[0m",n);fputs(tl,stdout);}
                    break;}
                continue;}                                               /* ignore arrows etc */
            RFIT;b[n++]=(char)c;(void)!write(1,&c,1);}
        (void)!write(1,"\n",1);b[n]=0;if(quit||!n)break;fn(b);}
    (void)!write(1,"\x1b[?2004l\n",9);tcsetattr(0,TCSAFLUSH,&o);
    #undef RFIT
}


static struct termios raw_orig;
static void raw_enter(void){struct termios r;tcgetattr(0,&raw_orig);r=raw_orig;
    r.c_lflag&=~(tcflag_t)(ICANON|ECHO);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSAFLUSH,&r);}
static void raw_exit(void){tcsetattr(0,TCSAFLUSH,&raw_orig);}
static int raw_key(void){unsigned char c;return read(STDIN_FILENO,&c,1)==1?(int)c:-1;}
static int raw_line(const char*prompt,char*buf,int sz){
    raw_exit();printf("%s",prompt);fflush(stdout);
    int ok=fgets(buf,sz,stdin)!=NULL;if(ok)buf[strcspn(buf,"\n")]=0;
    raw_enter();return ok&&buf[0];}
static int m_pick(const char*cat,const char*const*items,int n,char*out,size_t osz);


static const char*clip_cmd(void){static char c[64];   /* never read tmux copy-command back: default "" poisoned the conf */
    if(!c[0]){pcmd("command -v wl-copy pbcopy termux-clipboard-set 2>/dev/null",c,64);c[strcspn(c,"\n")]=0;}
    return c[0]?c:getenv("TMUX")?"tmux load-buffer -":NULL;}
static int to_clip(const char*d){const char*c=clip_cmd();if(!c)return 1;
    char cm[80];snprintf(cm,80,"%s 2>/dev/null",c);signal(SIGPIPE,SIG_IGN);
    FILE*f=popen(cm,"w");if(!f)return 1;fputs(d,f);return pclose(f);}
/* kill gate: every attempt logs ancestry to tmuxdeaths.log; headless refused without 'now', tty passes */
static int tmux_kill_gate(const char*cmd,int ok){
    char b[B];snprintf(b,B,"{ printf '%%s a %s tty=%%s ok=%d by: ' \"$(date '+%%F %%T')\" \"$([ -t 0 ]&&echo 1||echo 0)\";"
        "ps -o args= -p %d 2>/dev/null|head -c100|tr '\\n' ' ';printf ' <- ';"
        "ps -o args= -p $(ps -o ppid= -p %d 2>/dev/null) 2>/dev/null|head -c100;echo;} >>\"$HOME/a/adata/local/tmuxdeaths.log\" 2>/dev/null",
        cmd,ok,(int)getppid(),(int)getppid());
    (void)!system(b);
    if(!ok)printf("x refused: headless tmux-server kill takes down every agent on this box\n  intended? append now: a %s now\n",cmd);
    return ok;}
