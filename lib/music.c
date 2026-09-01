/* a music [term|yt q|get id] — cache-first gdrive music + youtube. menu: digits play, [s]earch→digit = save+play, [a]rchive = rm local (cloud+.index keep the how-to-get). termux: pkg yt-dlp rclone termux-api ffmpeg (no ffmpeg = every trim silently 0 0); 403: pip -U yt-dlp or retry */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#define Y "yt-dlp --js-runtimes node --remote-components ejs:github --no-warnings"
static char R[80]="music",C[256],L[64][256],ID[9][16];static int N,NI;
static struct termios T0;
static void nop(int s){(void)s;}
static void raw(int on){struct termios t=T0;if(on)t.c_lflag&=~(unsigned)(ICANON|ECHO);tcsetattr(0,TCSANOW,&t);}
static char key(void){raw(1);char k=0;(void)!read(0,&k,1);raw(0);return k;}
static int sh(const char*f,...){char b[2048];va_list a;va_start(a,f);vsnprintf(b,2048,f,a);va_end(a);return system(b);}
static void lines(const char*f,...){char b[1200];va_list a;va_start(a,f);vsnprintf(b,1200,f,a);va_end(a);
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
static void cfg(const char*set){   /* .cfg = "<cap-GB> <clip>"; cap 0 = keep everything */
    setenv("K",set?set:"",1);
    sh("cd \"%s\";[ -s .cfg ]||echo '5 1'>.cfg;set -- $(cat .cfg);A=$1;B=$2;"
       "case \"$K\" in cap-*)A=${K#cap-};;trim-*)B=${K#trim-};;esac;"
       "echo \"$A $B\">.cfg;printf '%%s %%s %%s' \"$A\" \"$B\" \"$(du -sm . 2>&-|cut -f1)\"",C);}
static void prune(void);
static void prune(void){   /* stay under the cap by dropping the oldest tracks — the .index line stays, so any of them is one tap from coming back */
    sh("cd \"%s\";[ -s .cfg ]||exit 0;set -- $(cat .cfg);L=$(awk -v g=\"$1\" 'BEGIN{printf \"%%d\",g*1024}');[ \"$L\" -gt 0 ]||exit 0;"
       "while [ $(du -sm . 2>&-|cut -f1) -gt $L ];do f=$(ls -1tr 2>&-|grep -v '\\.part$'|sed q);[ -n \"$f\" ]||break;rm -f \"$f\";done",C);}
static void trims(const char*id,const char*f,int full){   /* "<skip-in> <stop-at>": dead air at the two ends, so a tap starts on the first real sound. Future: sponsorblock music_offtopic API (id-keyed, pre-download) could feed this; not needed now */
    setenv("F",f,1);setenv("I",id,1);
    sh("cd \"%s\";command -v ffmpeg>/dev/null||pkg install -y ffmpeg>/dev/null 2>&1;[ -s .trim ]&&awk -v k=\"$I\" 'substr($0,1,length(k)+1)==k\" \"{f=1}END{exit !f}' .trim&&%s exit 0;"
       "D=$(ffprobe -v error -show_entries format=duration -of csv=p=0 \"$F\");"
       "S=$(ffmpeg -hide_banner -nostats -v info -t 20 -i \"$F\" -af silencedetect=noise=-91dB:d=0.05 -f null - 2>&1|grep -o 'silence_[a-z]*: [0-9.]*'|awk '$1==\"silence_start:\"{n++;st=$2+0}$1==\"silence_end:\"{if(n==1&&st<0.05)print $2;exit}');"
       "E=$([ %d = 1 ]&&ffmpeg -hide_banner -nostats -v info -sseof -20 -i \"$F\" -af silencedetect=noise=-91dB:d=0.05 -f null - 2>&1|grep -o 'silence_[a-z]*: [0-9.]*'|awk -v D=$D 'BEGIN{o=D>25?D-20:0}$1==\"silence_start:\"{s=$2}$1==\"silence_end:\"{e=$2}END{if(s!=\"\"&&D-(o+e)<0.5)printf \"%%.3f\",o+s}');"
       "awk -v k=\"$I\" 'substr($0,1,length(k)+1)!=k\" \"' .trim 2>&->.t$$;mv .t$$ .trim;echo \"$I ${S:-0} ${E:-0}\">>.trim",C,full?"[ $(awk -v k=\"$I\" 'substr($0,1,length(k)+1)==k\" \"{print $NF;exit}' .trim) != 0 ]&&":"",full);}
static char*get(const char*id){
    static char f[256];char p[600];
    snprintf(p,600,"%s/.lk%s",C,id);int lk=open(p,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lk>=0)flock(lk,LOCK_EX);   /* CLOEXEC: without it the backgrounded rclone inherits the fd and holds the lock for its whole upload (24s) */   /* one download per id: a tap during the prefetch waits for it instead of racing a second yt-dlp */
    lines("sed -n 's|^%s  ||p' \"%s/.index\" 2>&-|sed q",id,C);   /* cache hit: skip yt-dlp — it cost 2.4s just to answer "already downloaded" */
    if(N){snprintf(f,256,"%s",L[0]);snprintf(p,600,"%s/%s",C,f);if(!access(p,F_OK))return f;}
    for(int t=0;;t++){lines(Y" -f ba -o \"%s/%%(title)s.%%(ext)s\" --print after_move:filepath 'youtu.be/%s'",C,id);if(N)break;   /* 403 = stale yt-dlp (monthly): update once, retry */
        if(t)exit(1);sh("yt-dlp -U 2>&-||pip install -q -U yt-dlp --user --break-system-packages 2>&-");}
    snprintf(f,256,"%s",strrchr(L[0],'/')+1);
    setenv("I",id,1);setenv("N2",f,1);sh("grep -q \"^$I  \" \"%s/.index\" 2>&-||echo \"$I  $N2\">>\"%s/.index\"",C,C);   /* the head already recorded it */
    snprintf(p,600,"%s/%s",C,f);trims(id,p,1);prune();   /* full file: now the stop-at end is knowable too */
    if(strcmp(R,"music"))sh("(rclone copy \"%s/%s\" %s&&rclone copy \"%s/.index\" %s)>/dev/null 2>&1 &",C,f,R,C,R);   /* bg: a full-quota upload must not stall play */
    return f;}
static void head(const char*id){   /* 10s head (160KB) — enough to start instantly; yt-dlp resumes THIS .part byte-exact if he really listens. URL never touches a C buffer: googlevideo links are ~1200 chars */
    setenv("I",id,1);
    lines("cd \"%s\";M=$(mktemp);" Y " -f ba --print urls --print filename -o '%%(title)s.%%(ext)s' \"youtu.be/$I\">$M 2>&-;"
          "U=$(sed -n 1p $M);F=$(sed -n 2p $M);rm -f $M;[ -n \"$F\" ]||exit;"
          "[ -e \"$F\" ]||curl -s -r 0-163839 \"$U\" -o \"$F.part\";"
          "grep -q \"^$I  \" .index 2>&-||echo \"$I  $F\">>.index;printf %%s \"$F\"",C);
    if(N){char hp[600];snprintf(hp,600,"%s/%s.part",C,L[0]);trims(id,hp,0);}}
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
    int s=(ac>1&&(!strcmp(av[1],"yt")||!strcmp(av[1],"y")||!strcmp(av[1],"get")||!strcmp(av[1],"g")||!strcmp(av[1],"pre")||!strcmp(av[1],"trim")||!strcmp(av[1],"cfg")))?2:1;
    char q[512]="";for(int i=s;i<ac;i++)snprintf(q+strlen(q),512-strlen(q),"%s%s",i>s?" ":"",av[i]);
    if(s==2&&av[1][0]=='y'){srch(q);return 0;}
    if(s==2&&av[1][0]=='c'){cfg(ac>2?av[2]:0);prune();return 0;}   /* a lowered cap takes effect on the spot */
    if(s==2&&av[1][0]=='t'){char f2[600],*nm=q;   /* trim <id|file> */
        lines("sed -n 's|^%s  ||p' \"%s/.index\" 2>&-|sed q",q,C);if(N)nm=L[0];
        snprintf(f2,600,"%s/%s",C,nm);if(!access(f2,F_OK))trims(q,f2,1);
        setenv("I",q,1);
        sh("cd \"%s\";[ -s .cfg ]&&[ \"$(cut -d' ' -f2 .cfg)\" = 0 ]&&{ echo '0 0';exit 0; };"
           "awk -v k=\"$I\" 'substr($0,1,length(k)+1)==k\" \"{print $(NF-1),$NF;exit}' \"%s/.trim\" 2>&-",C,C);return 0;}
    if(s==2&&av[1][0]=='p'){   /* pre <id...>: fetch every hit at once, first one started first, so a tap plays instantly; the next search kills this batch */
        char pf[600];snprintf(pf,600,"%s/.pre",C);
        FILE*k=fopen(pf,"r");if(k){int pd;while(fscanf(k,"%d",&pd)==1)kill(-pd,SIGTERM);fclose(k);}
        FILE*w=fopen(pf,"w");
        for(int i=2;i<ac;i++){pid_t ch=fork();
            if(!ch){setsid();int dn=open("/dev/null",O_WRONLY);dup2(dn,1);dup2(dn,2);head(av[i]);_exit(0);}
            if(w)fprintf(w,"%d\n",ch);}
        if(w)fclose(w);return 0;}
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
