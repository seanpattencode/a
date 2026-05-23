/* lib/v.c — mmap tail-follow viewer. j/k ↑/↓ space/b PgUp/PgDn g/G q. Event-driven via inotify. # experimental */
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif
static volatile sig_atomic_t v_winch=0;
static void v_sigwinch(int s){(void)s;v_winch=1;}
static size_t v_back(const char*m,size_t p,int n){
    int k=0;while(p>0&&k<=n){p--;if(m[p]=='\n')k++;}return p?p+1:0;}
static size_t v_fwd(const char*m,size_t s,size_t p,int n){
    while(n-->0&&p<s){while(p<s&&m[p]!='\n')p++;if(p<s)p++;}return p;}
static int cmd_v(int c,char**v){
    perf_disarm();
    if(c<3){puts("a v <file>");return 1;}
    int fd=open(v[2],O_RDONLY);if(fd<0){perror(v[2]);return 1;}
    struct stat st;fstat(fd,&st);size_t sz=(size_t)st.st_size;
    char*m=sz?mmap(0,sz,PROT_READ,MAP_SHARED,fd,0):NULL;
    if(sz&&m==MAP_FAILED){perror("mmap");return 1;}
    int ifd=-1;
#ifdef __linux__
    ifd=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    if(ifd>=0)(void)inotify_add_watch(ifd,v[2],IN_MODIFY);
#endif
    struct winsize w={0};ioctl(1,TIOCGWINSZ,&w);
    int H=w.ws_row?w.ws_row:24,W=w.ws_col?w.ws_col:80;
    size_t top=v_back(m,sz,H);int follow=1;
    struct termios o={0},r;int tt=isatty(0);
    if(tt){tcgetattr(0,&o);r=o;cfmakeraw(&r);tcsetattr(0,TCSANOW,&r);}
    signal(SIGWINCH,v_sigwinch);
    static char b[1<<17];
    (void)!write(1,"\033[?1049h",8);
    for(;;){
        if(v_winch){ioctl(1,TIOCGWINSZ,&w);H=w.ws_row?w.ws_row:24;W=w.ws_col?w.ws_col:80;v_winch=0;}
        struct stat s2;if(!fstat(fd,&s2)&&(size_t)s2.st_size!=sz){
            if(m)munmap(m,sz);sz=(size_t)s2.st_size;
            m=sz?mmap(0,sz,PROT_READ,MAP_SHARED,fd,0):NULL;
            if(sz&&m==MAP_FAILED){m=NULL;sz=0;}
            if(follow)top=v_back(m,sz,H);}
        size_t bl=(size_t)sprintf(b,"\033[H\033[2J");
        size_t p=top;int rr=0;
        while(p<sz&&rr<H){int col=0;
            while(p<sz&&m[p]!='\n'&&col<W){b[bl++]=m[p++];col++;}
            while(p<sz&&m[p]!='\n')p++;
            if(p<sz)p++;
            b[bl++]='\r';b[bl++]='\n';rr++;}
        (void)!write(1,b,bl);
        fd_set fs;FD_ZERO(&fs);FD_SET(0,&fs);
        int mx=0;if(ifd>=0){FD_SET(ifd,&fs);mx=ifd;}
        if(select(mx+1,&fs,NULL,NULL,NULL)<0){if(errno==EINTR)continue;break;}
        if(ifd>=0&&FD_ISSET(ifd,&fs)){char xb[4096];(void)!read(ifd,xb,sizeof(xb));continue;}
        char ch;if(read(0,&ch,1)!=1)break;
        if(ch==3||ch=='q')break;
        else if(ch=='j')top=v_fwd(m,sz,top,1),follow=0;
        else if(ch=='k')top=v_back(m,top,1),follow=0;
        else if(ch==' ')top=v_fwd(m,sz,top,H),follow=0;
        else if(ch=='b')top=v_back(m,top,H),follow=0;
        else if(ch=='G'){top=v_back(m,sz,H);follow=1;}
        else if(ch=='g'){top=0;follow=0;}
        else if(ch==27){char s[8];int n=(int)read(0,s,8);
            if(n>=2&&s[0]=='['){if(s[1]=='A')top=v_back(m,top,1),follow=0;
                else if(s[1]=='B')top=v_fwd(m,sz,top,1),follow=0;
                else if(s[1]=='5')top=v_back(m,top,H),follow=0;
                else if(s[1]=='6')top=v_fwd(m,sz,top,H),follow=0;}}}
    (void)!write(1,"\033[?1049l",8);
    if(tt)tcsetattr(0,TCSANOW,&o);
    return 0;}
