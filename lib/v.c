/* lib/v.c — mmap tail-follow viewer. j/k ↑↓ space/b g/G q; tap-scroll via SGR mouse. # experimental */
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif
static size_t v_back(const char*m,size_t p,int n){int k=0;while(p>0&&k<=n){p--;if(m[p]=='\n')k++;}return p?p+1:0;}
static size_t v_fwd(const char*m,size_t s,size_t p,int n){while(n-->0&&p<s){while(p<s&&m[p]!='\n')p++;if(p<s)p++;}return p;}
static int cmd_v(int c,char**v){
    perf_disarm();
    if(c<3){puts("a v <file>");return 1;}
    int fd=open(v[2],O_RDONLY);if(fd<0){perror(v[2]);return 1;}
    int ifd=-1;
#ifdef __linux__
    ifd=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    if(ifd>=0)(void)inotify_add_watch(ifd,v[2],IN_MODIFY);
#endif
    struct termios o={0},r;int tt=isatty(0);
    if(tt){tcgetattr(0,&o);r=o;cfmakeraw(&r);tcsetattr(0,TCSANOW,&r);}
    (void)!write(1,"\033[?1000h\033[?1006h",16);
    static char b[1<<17];size_t sz=0,top=0;char*m=NULL;int follow=1;
    for(;;){
        struct winsize w={0};ioctl(1,TIOCGWINSZ,&w);
        int H=w.ws_row?w.ws_row:24,W=w.ws_col?w.ws_col:80;
        struct stat st;if(!fstat(fd,&st)&&(size_t)st.st_size!=sz){
            if(m)munmap(m,sz);sz=(size_t)st.st_size;
            m=sz?mmap(0,sz,PROT_READ,MAP_SHARED,fd,0):NULL;
            if(sz&&m==MAP_FAILED){m=NULL;sz=0;}
            if(follow)top=v_back(m,sz,H);}
        size_t bl=(size_t)sprintf(b,"\033[H\033[2J"),p=top;int rr=0;
        while(p<sz&&rr<H){int col=0;
            while(p<sz&&m[p]!='\n'&&col<W){b[bl++]=m[p++];col++;}
            if(p<sz&&m[p]=='\n')p++;
            b[bl++]='\r';b[bl++]='\n';rr++;}
        (void)!write(1,b,bl);
        fd_set fs;FD_ZERO(&fs);FD_SET(0,&fs);int mx=ifd>0?ifd:0;
        if(ifd>=0)FD_SET(ifd,&fs);
        if(select(mx+1,&fs,NULL,NULL,NULL)<0){if(errno==EINTR)continue;break;}
        if(ifd>=0&&FD_ISSET(ifd,&fs)){char xb[4096];(void)!read(ifd,xb,sizeof(xb));continue;}
        char ch;if(read(0,&ch,1)!=1)break;
        int d=0;
        if(ch==3||ch=='q')break;
        else if(ch=='j')d=1;else if(ch=='k')d=-1;
        else if(ch==' ')d=H;else if(ch=='b')d=-H;
        else if(ch=='g'){top=0;follow=0;}
        else if(ch=='G'){top=v_back(m,sz,H);follow=1;}
        else if(ch==27){char s[33];int n=(int)read(0,s,32);s[n<0?0:n]=0;
            if(n>=2&&s[0]=='['){if(s[1]=='A')d=-1;else if(s[1]=='B')d=1;
                else if(s[1]=='5')d=-H;else if(s[1]=='6')d=H;
                else if(s[1]=='<'){int btn=atoi(s+2);if(btn==64)d=-3;else if(btn==65)d=3;}}}
        if(d){top=d<0?v_back(m,top,-d):v_fwd(m,sz,top,d);follow=0;}}
    (void)!write(1,"\033[?1000l\033[?1006l",16);
    if(tt)tcsetattr(0,TCSANOW,&o);
    return 0;}
