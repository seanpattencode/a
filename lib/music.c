/* a music [term|yt q|get id] — cache-first gdrive music + youtube. menu: digits play, [s]earch→digit = save+play, [a]rchive = rm local (cloud+.index keep the how-to-get). termux: pkg yt-dlp rclone termux-api; 403: pip -U yt-dlp or retry */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/wait.h>
#define Y "yt-dlp --js-runtimes node --remote-components ejs:github --no-warnings"
static char R[80]="music",C[256],L[64][256],ID[9][16];static int N,NI;
static struct termios T0;
static void nop(int s){(void)s;}
static void raw(int on){struct termios t=T0;if(on)t.c_lflag&=~(unsigned)(ICANON|ECHO);tcsetattr(0,TCSANOW,&t);}
static char key(void){raw(1);char k=0;(void)!read(0,&k,1);raw(0);return k;}
static int sh(const char*f,...){char b[2048];va_list a;va_start(a,f);vsnprintf(b,2048,f,a);va_end(a);return system(b);}
static void lines(const char*f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,512,f,a);va_end(a);
    N=0;FILE*p=popen(b,"r");if(!p)return;
    while(N<64&&fgets(L[N],256,p)){L[N][strcspn(L[N],"\n")]=0;if(L[N][0])N++;}pclose(p);}
static void ff(const char*p){execlp("ffplay","ffplay","-nodisp","-autoexit","-v","16",p,(char*)0);_exit(1);}
static void play(const char*f,const char*src){printf("playing %s: %s\n",src,f);fflush(stdout);
    char p[512];snprintf(p,512,"%s/%s",C,f);
    if(getenv("TERMUX_VERSION")){execlp("termux-media-player","termux-media-player","play",p,(char*)0);exit(1);}
    if(!isatty(0))ff(p);
    pid_t k=fork();if(!k)ff(p);
    puts("\033[90m[esc] stop\033[0m");signal(SIGCHLD,nop);raw(1);
    char c;(void)!read(0,&c,1);raw(0);kill(k,SIGTERM);waitpid(k,0,0);exit(0);}
static char*get(const char*id){
    for(int t=0;;t++){lines(Y" -f ba -o \"%s/%%(title)s.%%(ext)s\" --print after_move:filepath 'youtu.be/%s'",C,id);if(N)break;   /* 403 = stale yt-dlp (monthly): update once, retry */
        if(t)exit(1);sh("yt-dlp -U 2>&-||pip install -q -U yt-dlp --user --break-system-packages 2>&-");}
    static char f[256];snprintf(f,256,"%s",strrchr(L[0],'/')+1);
    sh("echo \"%s  %s\">>\"%s/.index\"",id,f,C);
    if(strcmp(R,"music"))sh("(rclone copy \"%s/%s\" %s&&rclone copy \"%s/.index\" %s)>/dev/null 2>&1 &",C,f,R,C,R);   /* bg: a full-quota upload must not stall play */
    return f;}
static void srch(const char*q){char b[1024];NI=0;
    snprintf(b,1024,Y" 'ytsearch5:%s' -O '%%(id)s %%(title).60s %%(duration_string)s'",q);
    FILE*p=popen(b,"r");char o[512];
    while(p&&NI<9&&fgets(o,512,p)){char*sp=strchr(o,' ');if(!sp)continue;*sp=0;
        snprintf(ID[NI],16,"%s",o);if(isatty(0))printf("%d %s",NI+1,sp+1);else printf("%s\t%s",ID[NI],sp+1);NI++;}
    if(p)pclose(p);
    if(!NI||!isatty(0))return;
    printf("\033[90m1-%d save+play · other=quit\033[0m\n",NI);
    char k=key();if(k>'0'&&k<='9'&&k-'1'<NI)play(get(ID[k-'1']),"new");}
static void term(const char*q){
    for(int i=0;i<N;i++)if(strcasestr(L[i],q))play(L[i],"cache");
    printf("\xe2\x86\x93%s\n",R);fflush(stdout);
    lines("rclone lsf %s 2>&-",R);
    for(int i=0;i<N;i++)if(strcasestr(L[i],q)){if(sh("rclone copy \"%s/%s\" \"%s\"",R,L[i],C))exit(1);play(L[i],R);}
    printf("x %s\n",q);exit(1);}
int main(int ac,char**av){
    lines("rclone listremotes 2>&-");
    for(int i=0;i<N;i++)if(strstr(L[i],"a-gdrive")){snprintf(R,80,"%smusic",L[i]);break;}
    snprintf(C,256,"%s/a/adata/local/music",getenv("HOME"));sh("mkdir -p \"%s\"",C);
    if(isatty(0))tcgetattr(0,&T0);
    int s=(ac>1&&(!strcmp(av[1],"yt")||!strcmp(av[1],"y")||!strcmp(av[1],"get")||!strcmp(av[1],"g")))?2:1;
    char q[512]="";for(int i=s;i<ac;i++)snprintf(q+strlen(q),512-strlen(q),"%s%s",i>s?" ":"",av[i]);
    if(s==2&&av[1][0]=='y'){srch(q);return 0;}
    if(s==2){puts(get(q));return 0;}
    lines("ls \"%s\" 2>&-",C);
    if(ac>1)term(q);
    if(!isatty(0)){for(int i=0;i<N;i++)puts(L[i]);return 0;}
    printf("%s %s\n",R,C);
    if(!N)lines("rclone lsf %s 2>&-",R);
    for(int i=0;i<N&&i<9;i++)printf("%d %s\n",i+1,L[i]);
    fputs("\033[90m[s]earch [a]rchive\033[0m\n",stdout);
    char k=key();
    if(k=='s'||k=='a'){char in[256]="";printf(k=='s'?"search:":"archive #:");fflush(stdout);
        if(k=='s'){if(!fgets(in,256,stdin))return 0;in[strcspn(in,"\n")]=0;if(in[0])srch(in);return 0;}
        char d=key();puts("");
        if(d>'0'&&d<='9'&&d-'1'<N){char p[512];snprintf(p,512,"%s/%s",C,L[d-'1']);
            if(remove(p))return 1;printf("archived (cloud+.index keep it): %s\n",L[d-'1']);}
        return 0;}
    if(k>'0'&&k<='9'&&k-'1'<N)play(L[k-'1'],"cache");
    return 0;}
