/* fvid.c — fleet video: screen (wlr-screencopy) or camera (V4L2) → MJPEG multipart HTTP.
   Pure C, no pairing: trust = whoever can reach the port (bind LAN/localhost; tunnel over ssh).
   Newest-frame-wins: per-client TIOCOUTQ backlog check — laggy client skips frames, never stalls others.
   Every frame part carries X-TS (capture epoch µs) + X-Seq → client can compute frame age live.

   build:  wayland-scanner client-header wlr-screencopy-unstable-v1.xml wlr-screencopy.h
           wayland-scanner private-code  wlr-screencopy-unstable-v1.xml wlr-screencopy.c
           cc -O2 -w fvid.c wlr-screencopy.c -lwayland-client -lturbojpeg -o fvid
   run:    ./fvid -s [-o DP-1] [-p 9333] [-q 45] [-d 2] [-f 60]     screen mode
           ./fvid -c /dev/video0 [-p 9333] [-w 1280] [-h 720]       camera mode (HW MJPEG passthrough)
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/videodev2.h>
#include <turbojpeg.h>
#include <wayland-client.h>
#include "fvid-screencopy.h"

/* ---------- tiny http mjpeg fanout ---------- */
#define MAXC 8
static int cl[MAXC];
static int nskip[MAXC];
static long long t0us;
static long long nowus(void){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}

static int srv_open(int port){
    int s=socket(AF_INET,SOCK_STREAM,0);int one=1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={INADDR_ANY}};
    if(bind(s,(void*)&a,sizeof a)||listen(s,4)){perror("bind");exit(1);}
    for(int i=0;i<MAXC;i++)cl[i]=-1;
    return s;}

/* wrapper page: <img> renders the stream (Chrome won't render multipart on direct-nav),
   + fetch-based HUD reading X-TS/X-Seq for live fps, inter-frame gap, skipped-seq, KB.
   (absolute glass-to-glass is measured externally; phone/host clocks differ, so HUD shows
   client-relative metrics that need no clock sync.) */
static const char PAGE[]=
"HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nCache-Control:no-store\r\n\r\n"
"<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<style>html,body{margin:0;background:#000;height:100%;overflow:hidden}"
"img{width:100vw;height:100vh;object-fit:contain}"
"#h{position:fixed;top:0;left:0;font:12px monospace;color:#6f6;background:#000a;padding:3px 7px;white-space:pre}</style>"
"<img src=/s><div id=h>connecting</div>"
"<script>let n=0,t0=performance.now(),last=0,gap=0,seq=-1,miss=0,kb=0;"
"fetch('/s').then(r=>{let rd=r.body.getReader(),buf='';"
"function pump(){return rd.read().then(({done,value})=>{if(done)return;"
"buf+=new TextDecoder().decode(value.slice(0,Math.min(value.length,400)));"
"let m;while((m=buf.match(/X-TS:(\\d+)\\r\\nX-Seq:(\\d+)/))){let s=+m[2];"
"if(seq>=0&&s>seq+1)miss+=s-seq-1;seq=s;n++;let now=performance.now();gap=now-last;last=now;buf=buf.slice(m.index+m[0].length);}"
"pump();})}pump();});"
"setInterval(()=>{let fps=n/((performance.now()-t0)/1000);"
"h.textContent='fps '+fps.toFixed(1)+'  gap '+gap.toFixed(0)+'ms  frames '+n+'  missed '+miss;},500);"
"</script>";

static void srv_accept(int s){
    int c=accept(s,0,0);if(c<0)return;
    int one=1;setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
    char req[2048];req[0]=0;                                        /* wait for the GET line (don't race it) */
    struct pollfd rp={c,POLLIN};int rn=0;
    if(poll(&rp,1,500)>0)rn=recv(c,req,sizeof req-1,0);
    if(rn>0)req[rn]=0;else{close(c);return;}
    if(strncmp(req,"GET /s ",7)&&strncmp(req,"GET /s?",7)){        /* anything but /s -> wrapper page */
        write(c,PAGE,sizeof PAGE-1);close(c);return;}
    static const char H[]="HTTP/1.1 200 OK\r\nContent-Type:multipart/x-mixed-replace;boundary=f\r\nCache-Control:no-store\r\nAccess-Control-Allow-Origin:*\r\n\r\n";
    if(write(c,H,sizeof H-1)<0){close(c);return;}
    int fl=fcntl(c,F_GETFL);fcntl(c,F_SETFL,fl|O_NONBLOCK);
    for(int i=0;i<MAXC;i++)if(cl[i]<0){cl[i]=c;nskip[i]=0;fprintf(stderr,"client[%d] connected\n",i);return;}
    close(c);}

static void srv_send(unsigned char*jpg,int len,long long ts,long long seq){
    char hd[160];
    for(int i=0;i<MAXC;i++){
        if(cl[i]<0)continue;
        int pend=0;ioctl(cl[i],TIOCOUTQ,&pend);
        if(pend>65536){nskip[i]++;continue;}                   /* newest-frame-wins: skip laggards */
        int hl=snprintf(hd,sizeof hd,"--f\r\nContent-Type:image/jpeg\r\nContent-Length:%d\r\nX-TS:%lld\r\nX-Seq:%lld\r\n\r\n",len,ts,seq);
        if(write(cl[i],hd,hl)<0||write(cl[i],jpg,len)<0||write(cl[i],"\r\n",2)<0){
            close(cl[i]);cl[i]=-1;fprintf(stderr,"client[%d] gone (%d skips)\n",i,nskip[i]);}}}

static void stats(long long seq,double encms,int kb,int drops){
    static long long lastseq;static long long lastt;
    long long t=nowus();
    if(t-lastt<2000000)return;
    double fps=(seq-lastseq)*1e6/(double)(t-lastt);
    int c=0;for(int i=0;i<MAXC;i++)if(cl[i]>=0)c++;
    fprintf(stderr,"[fvid] %5.1f fps  enc %.1fms  %dKB  clients %d  skips %d\n",fps,encms,kb,c,drops);
    lastseq=seq;lastt=t;}

/* ---------- screen mode: wlr-screencopy ---------- */
static struct wl_shm*shm;static struct zwlr_screencopy_manager_v1*mgr;
static struct wl_output*outs[8];static char onames[8][64];static int nout;
static struct { struct wl_buffer*buf;void*data;int w,h,stride;uint32_t fmt;size_t sz; } sb;
static int frame_ready,frame_failed,yflip;

static void o_geo(void*d,struct wl_output*o,int x,int y,int pw,int ph,int sp,const char*mk,const char*md,int tr){}
static void o_mode(void*d,struct wl_output*o,uint32_t f,int w,int h,int r){}
static void o_done(void*d,struct wl_output*o){}
static void o_scale(void*d,struct wl_output*o,int s){}
static void o_name(void*d,struct wl_output*o,const char*n){for(int i=0;i<nout;i++)if(outs[i]==o)snprintf(onames[i],64,"%s",n);}
static void o_desc(void*d,struct wl_output*o,const char*n){}
static const struct wl_output_listener OL={o_geo,o_mode,o_done,o_scale,o_name,o_desc};

static void reg_add(void*d,struct wl_registry*r,uint32_t id,const char*i,uint32_t v){
    if(!strcmp(i,"wl_shm"))shm=wl_registry_bind(r,id,&wl_shm_interface,1);
    else if(!strcmp(i,"zwlr_screencopy_manager_v1"))mgr=wl_registry_bind(r,id,&zwlr_screencopy_manager_v1_interface,v<3?v:3);
    else if(!strcmp(i,"wl_output")&&nout<8){outs[nout]=wl_registry_bind(r,id,&wl_output_interface,v<4?v:4);
        wl_output_add_listener(outs[nout],&OL,0);nout++;}}
static void reg_rm(void*d,struct wl_registry*r,uint32_t id){}
static const struct wl_registry_listener RL={reg_add,reg_rm};

static void f_buffer(void*d,struct zwlr_screencopy_frame_v1*f,uint32_t fmt,uint32_t w,uint32_t h,uint32_t stride){
    if(sb.data&&(sb.w!=(int)w||sb.h!=(int)h||sb.stride!=(int)stride)){munmap(sb.data,sb.sz);wl_buffer_destroy(sb.buf);sb.data=0;}
    if(!sb.data){
        sb.w=w;sb.h=h;sb.stride=stride;sb.fmt=fmt;sb.sz=(size_t)stride*h;
        int fd=memfd_create("fvid",0);ftruncate(fd,sb.sz);
        sb.data=mmap(0,sb.sz,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        struct wl_shm_pool*p=wl_shm_create_pool(shm,fd,sb.sz);
        sb.buf=wl_shm_pool_create_buffer(p,0,w,h,stride,fmt);
        wl_shm_pool_destroy(p);close(fd);}}
static void f_flags(void*d,struct zwlr_screencopy_frame_v1*f,uint32_t fl){yflip=fl&ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;}
static void f_ready(void*d,struct zwlr_screencopy_frame_v1*f,uint32_t sh,uint32_t sl,uint32_t ns){frame_ready=1;}
static void f_failed(void*d,struct zwlr_screencopy_frame_v1*f){frame_failed=1;}
static void f_damage(void*d,struct zwlr_screencopy_frame_v1*f,uint32_t x,uint32_t y,uint32_t w,uint32_t h){}
static void f_linux_dmabuf(void*d,struct zwlr_screencopy_frame_v1*f,uint32_t fmt,uint32_t w,uint32_t h){}
static void f_buffer_done(void*d,struct zwlr_screencopy_frame_v1*f){
    zwlr_screencopy_frame_v1_copy(f,sb.buf);}
static const struct zwlr_screencopy_frame_v1_listener FL={f_buffer,f_flags,f_ready,f_failed,f_damage,f_linux_dmabuf,f_buffer_done};

static int run_screen(const char*oname,int port,int q,int dec,int maxfps){
    struct wl_display*dpy=wl_display_connect(NULL);
    if(!dpy){fprintf(stderr,"no wayland display\n");return 1;}
    struct wl_registry*reg=wl_display_get_registry(dpy);
    wl_registry_add_listener(reg,&RL,0);
    wl_display_roundtrip(dpy);wl_display_roundtrip(dpy);   /* globals + output names */
    if(!mgr){fprintf(stderr,"compositor lacks zwlr_screencopy_manager_v1\n");return 1;}
    int oi=0;for(int i=0;i<nout;i++)if(oname&&!strcmp(onames[i],oname))oi=i;
    fprintf(stderr,"capturing %s (of %d outputs), q=%d dec=%d maxfps=%d port=%d\n",onames[oi],nout,q,dec,maxfps,port);
    int s=srv_open(port);
    tjhandle tj=tjInitCompress();
    unsigned char*jbuf=NULL;unsigned long jcap=0;
    unsigned char*half=NULL;long long seq=0,lastframe=0;double encavg=0;
    for(;;){
        frame_ready=frame_failed=0;
        struct zwlr_screencopy_frame_v1*f=zwlr_screencopy_manager_v1_capture_output(mgr,1,outs[oi]);
        zwlr_screencopy_frame_v1_add_listener(f,&FL,0);
        while(!frame_ready&&!frame_failed){
            wl_display_flush(dpy);
            struct pollfd p[2]={{wl_display_get_fd(dpy),POLLIN},{s,POLLIN}};
            poll(p,2,100);
            if(p[1].revents&POLLIN)srv_accept(s);
            if(p[0].revents&POLLIN)wl_display_dispatch(dpy);else wl_display_dispatch_pending(dpy);}
        if(frame_failed){zwlr_screencopy_frame_v1_destroy(f);usleep(50000);continue;}
        long long ts=nowus();
        /* throttle */
        if(maxfps>0&&lastframe&&ts-lastframe<1000000LL/maxfps)usleep(1000000LL/maxfps-(ts-lastframe));
        lastframe=nowus();
        /* source pixels (optionally 2x2 box decimate; handles y-flip) */
        unsigned char*src=sb.data;int sw=sb.w,shh=sb.h,sstride=sb.stride;
        if(dec>1){
            int hw=sb.w/dec,hh=sb.h/dec;
            if(!half)half=malloc((size_t)hw*hh*4);
            for(int y=0;y<hh;y++){
                unsigned char*row0=(unsigned char*)sb.data+(size_t)(yflip?sb.h-1-y*dec:y*dec)*sb.stride;
                unsigned char*dst=half+(size_t)y*hw*4;
                for(int x=0;x<hw;x++){unsigned char*px=row0+(size_t)x*dec*4;
                    dst[x*4+0]=px[0];dst[x*4+1]=px[1];dst[x*4+2]=px[2];dst[x*4+3]=255;}}
            src=half;sw=hw;shh=hh;sstride=hw*4;}
        long long e0=nowus();
        tjCompress2(tj,src,sw,sstride,shh,TJPF_BGRX,&jbuf,&jcap,TJSAMP_420,q,TJFLAG_FASTDCT);
        double encms=(nowus()-e0)/1000.0;encavg=encavg?encavg*0.9+encms*0.1:encms;
        srv_send(jbuf,(int)jcap,ts,seq);
        int drops=0;for(int i=0;i<MAXC;i++)if(cl[i]>=0)drops+=nskip[i];
        stats(++seq,encavg,(int)jcap/1024,drops);
        zwlr_screencopy_frame_v1_destroy(f);}
    return 0;}

/* ---------- camera mode: V4L2 (HW MJPEG passthrough, else YUYV->JPEG) ---------- */
/* standard JPEG Huffman tables (some UVC cams omit DHT; browsers need it) */
static const unsigned char DHT[]={
0xFF,0xC4,0x01,0xA2,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
0x01,0x00,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7D,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08,0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,0x28,0x29,0x2A,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,
0x11,0x00,0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xA1,0xB1,0xC1,0x09,0x23,0x33,0x52,0xF0,0x15,0x62,0x72,0xD1,0x0A,0x16,0x24,0x34,0xE1,0x25,0xF1,0x17,0x18,0x19,0x1A,0x26,0x27,0x28,0x29,0x2A,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA};

static int has_dht(unsigned char*j,int n){for(int i=2;i+4<n&&i<2048;){if(j[i]!=0xFF)break;int m=j[i+1];if(m==0xC4)return 1;if(m==0xDA)return 0;i+=2+((j[i+2]<<8)|j[i+3]);}return 0;}

static int run_cam(const char*dev,int port,int w,int h){
    int fd=open(dev,O_RDWR);if(fd<0){perror(dev);return 1;}
    struct v4l2_format fm={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE};
    fm.fmt.pix.width=w;fm.fmt.pix.height=h;fm.fmt.pix.pixelformat=V4L2_PIX_FMT_MJPEG;fm.fmt.pix.field=V4L2_FIELD_NONE;
    if(ioctl(fd,VIDIOC_S_FMT,&fm)<0){perror("S_FMT");return 1;}
    int mj=fm.fmt.pix.pixelformat==V4L2_PIX_FMT_MJPEG;
    fprintf(stderr,"cam %s: %dx%d %s\n",dev,fm.fmt.pix.width,fm.fmt.pix.height,mj?"HW-MJPEG":"raw(unsupported here)");
    if(!mj){fprintf(stderr,"camera lacks MJPEG; YUYV path not built in this rev\n");return 1;}
    struct v4l2_requestbuffers rq={.count=4,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_MMAP};
    if(ioctl(fd,VIDIOC_REQBUFS,&rq)<0){perror("REQBUFS");return 1;}
    void*bufs[4];size_t blen[4];
    for(int i=0;i<(int)rq.count;i++){
        struct v4l2_buffer b={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_MMAP,.index=i};
        ioctl(fd,VIDIOC_QUERYBUF,&b);
        bufs[i]=mmap(0,b.length,PROT_READ,MAP_SHARED,fd,b.m.offset);blen[i]=b.length;
        ioctl(fd,VIDIOC_QBUF,&b);}
    int t=V4L2_BUF_TYPE_VIDEO_CAPTURE;ioctl(fd,VIDIOC_STREAMON,&t);
    int s=srv_open(port);
    unsigned char*out=malloc(4<<20);long long seq=0;
    for(;;){
        struct pollfd p[2]={{fd,POLLIN},{s,POLLIN}};
        poll(p,2,1000);
        if(p[1].revents&POLLIN)srv_accept(s);
        if(!(p[0].revents&POLLIN))continue;
        struct v4l2_buffer b={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_MMAP};
        if(ioctl(fd,VIDIOC_DQBUF,&b)<0)continue;
        long long ts=nowus();
        unsigned char*j=bufs[b.index];int n=b.bytesused;
        if(!has_dht(j,n)){                       /* splice standard tables before SOS */
            int i=2;while(i+4<n){if(j[i]!=0xFF)break;if(j[i+1]==0xDA)break;i+=2+((j[i+2]<<8)|j[i+3]);}
            memcpy(out,j,i);memcpy(out+i,DHT,sizeof DHT);memcpy(out+i+sizeof DHT,j+i,n-i);
            srv_send(out,n+sizeof DHT,ts,seq);}
        else srv_send(j,n,ts,seq);
        stats(++seq,0,b.bytesused/1024,0);
        ioctl(fd,VIDIOC_QBUF,&b);}
    return 0;}

int main(int argc,char**argv){
    const char*oname=NULL,*cam=NULL;int port=9333,q=45,dec=2,maxfps=60,w=1280,h=720,screen=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-s"))screen=1;
        else if(!strcmp(argv[i],"-c"))cam=argv[++i];
        else if(!strcmp(argv[i],"-o"))oname=argv[++i];
        else if(!strcmp(argv[i],"-p"))port=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-q"))q=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-d"))dec=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-f"))maxfps=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-w"))w=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-h"))h=atoi(argv[++i]);}
    signal(SIGPIPE,SIG_IGN);
    t0us=nowus();
    if(screen)return run_screen(oname,port,q,dec,maxfps);
    if(cam)return run_cam(cam,port,w,h);
    fprintf(stderr,"usage: fvid -s [-o OUTPUT] | -c /dev/videoN   [-p port -q qual -d decim -f maxfps]\n");
    return 1;}
