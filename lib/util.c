/* utilities */
#ifdef __ANDROID__
#define OPENER "termux-open"
#define APP_CMD "CLASSPATH=/data/data/com.termux/files/usr/libexec/termux-am/am.apk /system/bin/app_process / com.termux.termuxam.Am start --user 0 -n"
#elif defined(__APPLE__)
#define OPENER "open"
#define APP_CMD "open -a"
#else
#define OPENER "xdg-open"
#define APP_CMD "gtk-launch"
#endif
static void bg_exec(const char *c,const char *a){if(!fork()){setsid();int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(n,1);dup2(n,2);close(n);execlp(c,c,a,(char*)NULL);_exit(1);}}
static int fexists(const char *p) { struct stat s; return stat(p, &s) == 0; }
static int dexists(const char *p) { struct stat s; return stat(p, &s) == 0 && S_ISDIR(s.st_mode); }
static void mkdirp(const char *p) { char t[P]; snprintf(t,P,"%s",p); for(char*s=t+1;*s;s++) if(*s=='/'){*s=0;mkdir(t,0755);*s='/';} mkdir(t,0755); }

static char *readf(const char *p, size_t *len) {
    int fd = open(p, O_RDONLY); if (fd < 0) return NULL;
    struct stat s; if (fstat(fd, &s) < 0) { close(fd); return NULL; }
    size_t sz = (size_t)s.st_size;
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

static int pcmd(const char *cmd, char *out, int sz) {
    if (out) out[0] = 0;
    FILE *f = popen(cmd, "r"); if (!f) return -1;
    if (out) { int n = 0; char b[B];
        while (fgets(b, B, f) && n + (int)strlen(b) < sz - 1) n += sprintf(out + n, "%s", b);
    } else { char b[B]; while (fgets(b, B, f)) ; }
    return pclose(f);
}


static const char *bname(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* join argv[from..argc) with spaces into buf */
static int ajoin(char*b,int sz,int argc,char**argv,int from){int l=0;for(int i=from;i<argc;i++)l+=snprintf(b+l,(size_t)(sz-l),"%s%s",i>from?" ":"",argv[i]);return l;}

/* paste-aware fgets: bracketed paste (\x1b[200~..\x1b[201~) returned as one line; caller enables/disables ?2004h */
static size_t paste_line(char *b, size_t sz, FILE *fp) {
    size_t bl=0;int paste=0;char *p,*e;b[0]=0;
    for(;;){if(!fgets(b+bl,sz-bl,fp))return 0;
        bl+=strlen(b+bl);
        if((p=strstr(b,"\x1b[200~"))){memmove(p,p+6,strlen(p+6)+1);bl-=6;paste=1;continue;}
        if((e=strstr(b,"\x1b[201~"))){*e=0;return (size_t)(e-b);}
        if(!paste&&bl&&b[bl-1]=='\n'){b[--bl]=0;return bl;}}
}
/* rapid input loop — call fn(line) for each line, empty line exits; bracketed paste = one note */
static void rapid(const char *prompt, void (*fn)(const char*)) {
    if (!isatty(STDIN_FILENO)) return; perf_disarm();
    (void)!write(1,"\x1b[?2004h",8);
    static char b[65536];
    for(;;){fputs(prompt,stdout);fflush(stdout);if(!paste_line(b,sizeof b,stdin)||!*b)break;fn(b);}
    (void)!write(1,"\x1b[?2004l",8);
}

/* raw terminal helpers */
static struct termios raw_orig;
static void raw_enter(void){struct termios r;tcgetattr(0,&raw_orig);r=raw_orig;
    r.c_lflag&=~(tcflag_t)(ICANON|ECHO);r.c_cc[VMIN]=1;r.c_cc[VTIME]=0;tcsetattr(0,TCSAFLUSH,&r);}
static void raw_exit(void){tcsetattr(0,TCSAFLUSH,&raw_orig);}
static int raw_key(void){unsigned char c;return read(STDIN_FILENO,&c,1)==1?(int)c:-1;}
static int raw_line(const char*prompt,char*buf,int sz){
    raw_exit();printf("%s",prompt);fflush(stdout);
    int ok=fgets(buf,sz,stdin)!=NULL;if(ok)buf[strcspn(buf,"\n")]=0;
    raw_enter();return ok&&buf[0];}


static const char*clip_cmd(void){static char c[64];if(!getenv("TMUX"))return NULL;
    if(!pcmd("tmux show -sv copy-command 2>/dev/null",c,64)&&c[0]){c[strcspn(c,"\n")]=0;return c;}
    return "tmux load-buffer -";}
static int to_clip(const char*d){const char*c=clip_cmd();if(!c)return 1;
    char cm[80];snprintf(cm,80,"%s 2>/dev/null",c);
    FILE*f=popen(cm,"w");if(!f)return 1;fputs(d,f);return pclose(f);}
