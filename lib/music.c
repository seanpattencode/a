/* a music [term|yt q|play id|play stop|get id|pre id..|trim id|cfg|rm id] — cache-first gdrive+youtube. tty: digits play, [s]earch→digit save+play, [a]rchive rm local. piped: yt prints id<TAB>title rows, play <id> detaches (next play/stop replaces). termux deps: yt-dlp rclone termux-api ffmpeg */
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
static char*res(const char*q){lines("sed -n 's|^%s  ||p' \"%s/.index\" 2>&-|sed q",q,C);return N?L[0]:(char*)q;}   /* id → cached name via .index (N = hit) */
static char*fp(const char*f){static char p[880];snprintf(p,880,"%s/%s",C,f);return p;}
static void killf(const char*n){FILE*k=fopen(fp(n),"r");if(k){int d;while(fscanf(k,"%d",&d)==1)kill(-d,SIGTERM);fclose(k);}}
static pid_t bg(void){pid_t c=fork();if(!c){setsid();int d=open("/dev/null",O_WRONLY);dup2(d,1);dup2(d,2);}return c;}
static void ff(const char*p){execlp("ffplay","ffplay","-nodisp","-autoexit","-v","16",p,(char*)0);_exit(1);}
static void play(const char*f,const char*src){printf("playing %s: %s\n",src,f);fflush(stdout);
    char*p=fp(f);
    if(getenv("TERMUX_VERSION")){execlp("termux-media-player","termux-media-player","play",p,(char*)0);exit(1);}
    if(!isatty(0))ff(p);
    pid_t k=fork();if(!k)ff(p);
    puts("\033[90m[esc] stop\033[0m");signal(SIGCHLD,nop);raw(1);
    char c;(void)!read(0,&c,1);raw(0);kill(k,SIGTERM);waitpid(k,0,0);exit(0);}
static void cfg(const char*set){   /* .cfg = "<cap-GB> <clip>"; 0 = keep all */
    setenv("K",set?set:"",1);
    sh("cd \"%s\";[ -s .cfg ]||echo '5 1'>.cfg;set -- $(cat .cfg);A=$1;B=$2;"
       "case \"$K\" in cap-*)A=${K#cap-};;trim-*)B=${K#trim-};;esac;"
       "echo \"$A $B\">.cfg;printf '%%s %%s %%s' \"$A\" \"$B\" \"$(du -sm . 2>&-|cut -f1)\"",C);}
static void prune(void){   /* drop oldest past cap; .index keeps the how-to-get */
    sh("cd \"%s\";[ -s .cfg ]||exit 0;set -- $(cat .cfg);L=$(awk -v g=\"$1\" 'BEGIN{printf \"%%d\",g*1024}');[ \"$L\" -gt 0 ]||exit 0;"
       "while [ $(du -sm . 2>&-|cut -f1) -gt $L ];do f=$(ls -1tr 2>&-|grep -v '\\.part$'|sed q);[ -n \"$f\" ]||break;rm -f \"$f\";done",C);}
static void trims(const char*id,const char*f,int full){   /* .trim "<id> <skip-in> <stop-at>": dead air at both ends */
    setenv("F",f,1);setenv("I",id,1);
    sh("cd \"%s\";command -v ffmpeg>/dev/null||pkg install -y ffmpeg>/dev/null 2>&1;grep -q -- \"^$I \" .trim 2>&-&&%s exit 0;"
       "D=$(ffprobe -v error -show_entries format=duration -of csv=p=0 \"$F\");"
       "S=$(ffmpeg -v info -t 20 -i \"$F\" -af silencedetect=noise=-91dB:d=0.05 -f null - 2>&1|grep -o 'silence_[a-z]*: [0-9.]*'|awk '$1==\"silence_start:\"{n++;st=$2+0}$1==\"silence_end:\"{if(n==1&&st<0.05)print $2;exit}');"
       "E=$([ %d = 1 ]&&ffmpeg -v info -sseof -20 -i \"$F\" -af silencedetect=noise=-91dB:d=0.05 -f null - 2>&1|grep -o 'silence_[a-z]*: [0-9.]*'|awk -v D=$D 'BEGIN{o=D>25?D-20:0}$1==\"silence_start:\"{s=$2}$1==\"silence_end:\"{e=$2}END{if(s!=\"\"&&D-(o+e)<0.5)printf \"%%.3f\",o+s}');"
       "grep -v -- \"^$I \" .trim 2>&->.t$$;mv .t$$ .trim;echo \"$I ${S:-0} ${E:-0}\">>.trim",C,full?"[ $(awk -v k=\"$I\" 'substr($0,1,length(k)+1)==k\" \"{print $NF;exit}' .trim) != 0 ]&&":"",full);}
static char*get(const char*id){
    static char f[256];char p[600];
    snprintf(p,600,"%s/.lk%s",C,id);int lk=open(p,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lk>=0)flock(lk,LOCK_EX);   /* one dl per id; CLOEXEC or bg rclone holds the lock */
    res(id);
    if(N){snprintf(f,256,"%s",L[0]);if(!access(fp(f),F_OK))return f;}
    for(int t=0;;t++){lines(Y" -f ba -o \"%s/%%(title)s.%%(ext)s\" --print after_move:filepath 'youtu.be/%s'",C,id);if(N)break;   /* 403 = stale yt-dlp: update, retry once */
        if(t)exit(1);sh("yt-dlp -U 2>&-||pip install -q -U yt-dlp --user --break-system-packages 2>&-");}
    snprintf(f,256,"%s",strrchr(L[0],'/')+1);
    setenv("I",id,1);setenv("N2",f,1);sh("grep -q -- \"^$I  \" \"%s/.index\" 2>&-||echo \"$I  $N2\">>\"%s/.index\"",C,C);
    trims(id,fp(f),1);prune();
    if(strcmp(R,"music"))sh("(rclone copy \"%s/%s\" %s&&rclone copy \"%s/.index\" %s)>/dev/null 2>&1 &",C,f,R,C,R);
    return f;}
static void head(const char*id){   /* 160KB head = instant start; yt-dlp resumes the .part byte-exact; URL stays in shell (~1200 chars) */
    setenv("I",id,1);
    lines("cd \"%s\";M=$(mktemp);" Y " -f ba --print urls --print filename -o '%%(title)s.%%(ext)s' \"youtu.be/$I\">$M 2>&-;"
          "U=$(sed -n 1p $M);F=$(sed -n 2p $M);rm -f $M;[ -n \"$F\" ]||exit;"
          "[ -e \"$F\" ]||curl -s -r 0-163839 \"$U\" -o \"$F.part\";"
          "grep -q -- \"^$I  \" .index 2>&-||echo \"$I  $F\">>.index;printf %%s \"$F\"",C);
    if(N){char hp[300];snprintf(hp,300,"%s.part",L[0]);trims(id,fp(hp),0);}}
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
    for(int i=0;i<N;i++)if(strstr(L[i],"a-gdrive2:")){snprintf(R,80,"%smusic",L[i]);break;}
    snprintf(C,256,"%s/a/adata/local/music",getenv("HOME"));sh("mkdir -p \"%s\"",C);
    if(isatty(0))tcgetattr(0,&T0);
    char pat[16];snprintf(pat,16," %.12s ",ac>1?av[1]:"-");
    int s=strstr(" yt y get g play pre trim cfg rm ",pat)?2:1;
    char q[512]="";for(int i=s;i<ac;i++)snprintf(q+strlen(q),512-strlen(q),"%s%s",i>s?" ":"",av[i]);
    if(s==2&&av[1][0]=='y'){srch(q);return 0;}
    if(s==2&&av[1][0]=='c'){cfg(ac>2?av[2]:0);prune();return 0;}
    if(s==2&&av[1][0]=='r'){char*nm=res(q),b[300];   /* rm <id|file>: clear local bytes; .index keeps the how-to-get */
        int r1=!remove(fp(nm));snprintf(b,300,"%s.part",nm);int r2=!remove(fp(b));
        printf(r1||r2?"cleared %s%s\n":"x nothing local: %s%s\n",nm,r2?" (+part)":"");return 0;}
    if(s==2&&av[1][0]=='t'){char*nm=res(q),*x=fp(nm);
        if(!access(x,F_OK))trims(q,x,1);
        setenv("I",q,1);
        sh("cd \"%s\";[ -s .cfg ]&&[ \"$(cut -d' ' -f2 .cfg)\" = 0 ]&&{ echo '0 0';exit 0; };"
           "awk -v k=\"$I\" 'substr($0,1,length(k)+1)==k\" \"{print $(NF-1),$NF;exit}' \"%s/.trim\" 2>&-",C,C);return 0;}
    if(s==2&&av[1][0]=='p'&&av[1][1]=='l'){killf(".play");   /* play <id>|stop: tty = esc stops, piped = detach + receipt; new play replaces old */
        if(!strcmp(q,"stop")){puts("stopped");return 0;}
        char*f=get(q);
        if(isatty(0))play(f,"pick");
        pid_t ch=bg();if(!ch)ff(fp(f));
        FILE*w=fopen(fp(".play"),"w");if(w){fprintf(w,"%d\n",ch);fclose(w);}
        printf("playing: %s\nstop: a music play stop\n",f);return 0;}
    if(s==2&&av[1][0]=='p'){killf(".pre");   /* pre <id...>: prefetch every hit, first first; next search kills the batch */
        FILE*w=fopen(fp(".pre"),"w");
        for(int i=2;i<ac;i++){pid_t ch=bg();
            if(!ch){head(av[i]);_exit(0);}
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
        if(d>'0'&&d<='9'&&d-'1'<N){if(remove(fp(L[d-'1'])))return 1;
            printf("archived (cloud+.index keep it): %s\n",L[d-'1']);}
        return 0;}
    if(k>'0'&&k<='9'&&k-'1'<N)play(L[k-'1'],"cache");
    return 0;}
