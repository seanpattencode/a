# experimental
"""lib/tlt.py — native Android terminal, single-file APK builder.
Fork of text-launcher. PTY /system/bin/sh + minimal VT100 + on-screen keyboard.
Usage: python3 my/tlt.py [build]
"""
import os,sys,subprocess as S,glob,shutil

PKG="com.tlt"
H=os.path.expanduser("~")
MAC=sys.platform=="darwin"
SDK=os.environ.get("ANDROID_HOME") or (H+"/Library/Android/sdk" if MAC else H+"/Android/Sdk")
NDKS=sorted(glob.glob(SDK+"/ndk/*"))
if not NDKS:sys.exit(f"x NDK missing in {SDK}/ndk")
NDK=NDKS[-1]
HOST="darwin-x86_64" if MAC else "linux-x86_64"
CC=f"{NDK}/toolchains/llvm/prebuilt/{HOST}/bin/aarch64-linux-android26-clang"
if not os.access(CC,os.X_OK):sys.exit(f"x {CC}")
JH=os.environ.get("JAVA_HOME") or ("/Applications/Android Studio.app/Contents/jbr/Contents/Home" if MAC and os.path.isdir("/Applications/Android Studio.app/Contents/jbr/Contents/Home") else "/opt/homebrew/opt/openjdk@17")
if not os.path.isdir(JH):sys.exit("x no JAVA_HOME")
os.environ["JAVA_HOME"]=JH;os.environ["PATH"]=f"{JH}/bin:"+os.environ["PATH"]

R=os.path.dirname(os.path.abspath(__file__))
D=R+"/tlt_build"

KT=r'''package com.tlt
import android.app.Activity
import android.graphics.*
import android.os.*
import android.system.Os
import android.view.*
import java.io.File
class M:Activity(){
companion object{init{System.loadLibrary("t")}}
private val h=Handler(Looper.getMainLooper())
private var px:IntArray?=null
private lateinit var bmp:Bitmap
private lateinit var v:View
private external fun nResize(w:Int,hh:Int)
private external fun nFont(data:IntArray)
private external fun nRender(pixels:IntArray)
private external fun nTouch(action:Int,x:Float,y:Float)
private external fun nStart(libDir:String,filesDir:String)
private external fun nStop()
private external fun nDirty():Boolean
private fun setup(){
 val ld=applicationInfo.nativeLibraryDir
 val bin=File(filesDir,"bin");bin.mkdirs()
 for((n,so) in listOf("tmux" to "libtmux.so","tic" to "libtic.so","dbclient" to "libssh.so","a" to "libaa.so","ssh" to "libsshwrap.so","sshpass" to "libsshwrap.so","rclone" to "librclone.so")){
  val l=File(bin,n);try{Os.remove(l.absolutePath)}catch(e:Exception){}
  try{Os.symlink("$ld/$so",l.absolutePath)}catch(e:Exception){}}
 val ti=File(filesDir,"terminfo");ti.mkdirs()
 for(sub in listOf("x","s")){File(ti,sub).mkdirs()}
 for((name,sub) in listOf("xterm-256color" to "x","screen" to "s","screen-256color" to "s")){
  val f=File(ti,"$sub/$name");if(!f.exists())try{assets.open(name).use{i->f.outputStream().use{o->i.copyTo(o)}}}catch(e:Exception){}}}
private fun atlas(sz:Float):IntArray{
val tf=try{Typeface.createFromAsset(assets,"mono.ttf")}catch(e:Exception){Typeface.MONOSPACE}
val p=Paint().apply{textSize=sz;color=-1;isAntiAlias=true;typeface=tf}
val cw=p.measureText("M").toInt()
val tb=Rect()
val ch=(-p.ascent()+p.descent()).toInt()+1
val b=Bitmap.createBitmap(cw*95,ch,Bitmap.Config.ARGB_8888)
Canvas(b).let{c->for(i in 0 until 95){val s=((32+i).toChar()).toString();p.getTextBounds(s,0,1,tb);c.drawText(s,(i*cw-tb.left).toFloat(),-p.ascent(),p)}}
val r=IntArray(2+cw*95*ch);r[0]=cw;r[1]=ch
b.getPixels(r,2,cw*95,0,0,cw*95,ch);b.recycle();return r}
private val tick=object:Runnable{override fun run(){if(::bmp.isInitialized&&nDirty())v.invalidate();h.postDelayed(this,16)}}
override fun onCreate(b:Bundle?){super.onCreate(b)
window.decorView.systemUiVisibility=View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
v=object:View(this){
override fun onSizeChanged(w:Int,hh:Int,ow:Int,oh:Int){
if(::bmp.isInitialized)bmp.recycle()
bmp=Bitmap.createBitmap(w,hh,Bitmap.Config.ARGB_8888)
px=IntArray(w*hh);nResize(w,hh);nFont(atlas(40f))}
override fun onDraw(c:Canvas){val a=px?:return;if(!::bmp.isInitialized)return
nRender(a);bmp.setPixels(a,0,width,0,0,width,height);c.drawBitmap(bmp,0f,0f,null)}
override fun onTouchEvent(e:MotionEvent):Boolean{nTouch(e.action and 0xFF,e.x,e.y);invalidate();return true}}
setContentView(v);setup();nStart(applicationInfo.nativeLibraryDir,filesDir.absolutePath);h.post(tick)}
override fun onDestroy(){nStop();super.onDestroy()}}
'''

CSRC=r'''#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <android/log.h>
#define NC 95
#define MR 120
#define MC 300
typedef struct{uint32_t*px;int cw,ch,aw;}Font;
static Font FN;
typedef struct{uint8_t ch,fg,bg,at;}Cell;
static Cell G[MR*MC];
static int W,H,rows,cols,cx,cy;
static uint8_t cfg=7,cbg=0,cat=0;
static int pty_fd=-1;
static pthread_t rt;
static volatile int dirty=0,run_=0;
static int ctrl_stk=0;
static int pst=0,np=0,pb=0,params[16];
/* row 0 specials: \x01=ESC \x02=TAB \x03=CTRL \x04=LEFT \x05=DOWN \x06=UP \x07=RIGHT \x08=BS \x09=ENTER */
#define NROWS 6
static const char*KR[NROWS]={
 "\x01\x02\x03\x04\x05\x06\x07",
 "1234567890",
 "qwertyuiop",
 "asdfghjkl",
 "zxcvbnm,.\x08",
 "-= \x09"};
static float KB[60][4];
static int nkeys,pressed=-1;
static uint32_t PAL[16]={
 0xFF000000,0xFFCC0000,0xFF00CC00,0xFFCCCC00,0xFF2244CC,0xFFCC00CC,0xFF00CCCC,0xFFCCCCCC,
 0xFF666666,0xFFFF4444,0xFF44FF44,0xFFFFFF44,0xFF4488FF,0xFFFF44FF,0xFF44FFFF,0xFFFFFFFF};

static void cell_set(int r,int c,uint8_t ch){if(r>=0&&r<rows&&c>=0&&c<cols)G[r*MC+c]=(Cell){ch,cfg,cbg,cat};}
static void scroll1(void){memmove(G,G+MC,sizeof(Cell)*MC*(rows-1));for(int c=0;c<cols;c++)G[(rows-1)*MC+c]=(Cell){' ',cfg,cbg,0};}
static void put_ch(uint8_t c){if(cx>=cols){cx=0;cy++;}if(cy>=rows){scroll1();cy=rows-1;}cell_set(cy,cx,c);cx++;}
static void clmp(void){if(cx<0)cx=0;if(cx>=cols)cx=cols-1;if(cy<0)cy=0;if(cy>=rows)cy=rows-1;}
static void erase(int ra,int ca,int rb,int cb){for(int r=ra;r<=rb&&r<rows;r++){int s=(r==ra)?ca:0,e=(r==rb)?cb:cols-1;for(int c=s;c<=e&&c<cols;c++)cell_set(r,c,' ');}}
static void do_sgr(void){
 if(np==0){cfg=7;cbg=0;cat=0;return;}
 for(int i=0;i<np;i++){int p=params[i];
  if(p==0){cfg=7;cbg=0;cat=0;}
  else if(p==1)cat|=1;
  else if(p==22)cat&=~1;
  else if(p==7){uint8_t t=cfg;cfg=cbg;cbg=t;}
  else if(p>=30&&p<=37)cfg=p-30;
  else if(p==39)cfg=7;
  else if(p>=40&&p<=47)cbg=p-40;
  else if(p==49)cbg=0;
  else if(p>=90&&p<=97)cfg=p-90+8;
  else if(p>=100&&p<=107)cbg=p-100+8;}}
static void do_csi(int f){
 int p1=np>0?params[0]:0,p2=np>1?params[1]:0;
 switch(f){
 case 'H':case 'f':cy=(p1?p1:1)-1;cx=(p2?p2:1)-1;clmp();break;
 case 'A':cy-=p1?p1:1;clmp();break;
 case 'B':cy+=p1?p1:1;clmp();break;
 case 'C':cx+=p1?p1:1;clmp();break;
 case 'D':cx-=p1?p1:1;clmp();break;
 case 'G':cx=(p1?p1:1)-1;clmp();break;
 case 'd':cy=(p1?p1:1)-1;clmp();break;
 case 'J':
  if(p1==0)erase(cy,cx,rows-1,cols-1);
  else if(p1==1)erase(0,0,cy,cx);
  else erase(0,0,rows-1,cols-1);
  break;
 case 'K':
  if(p1==0)erase(cy,cx,cy,cols-1);
  else if(p1==1)erase(cy,0,cy,cx);
  else erase(cy,0,cy,cols-1);
  break;
 case 'm':do_sgr();break;
 case 'X':{int n=p1?p1:1;for(int i=0;i<n&&cx+i<cols;i++)cell_set(cy,cx+i,' ');}break;
 default:break;}}
static void feed(uint8_t b){
 if(pst==0){
  if(b==0x1B)pst=1;
  else if(b=='\r')cx=0;
  else if(b=='\n'){cy++;if(cy>=rows){scroll1();cy=rows-1;}}
  else if(b=='\b'||b==0x7F){if(cx>0)cx--;}
  else if(b=='\t'){cx=(cx+8)&~7;if(cx>=cols)cx=cols-1;}
  else if(b==0x07)return;
  else if(b>=32&&b<127)put_ch(b);
  else if(b>=0x80)put_ch('?');
 }else if(pst==1){
  if(b=='[')pst=2,np=0,pb=0;
  else if(b==']')pst=3;
  else if(b=='='||b=='>'||b=='7'||b=='8'||b=='M')pst=0;
  else if(b=='(')pst=5;
  else pst=0;
 }else if(pst==2){
  if(b>='0'&&b<='9')pb=pb*10+(b-'0');
  else if(b==';'){if(np<15){params[np++]=pb;pb=0;}}
  else if(b=='?'||b=='>'||b=='!')return;
  else if(b>=0x40&&b<=0x7E){if(np<15)params[np++]=pb;do_csi(b);pst=0;}
  else if(b>=0x20&&b<=0x3F)return;
  else pst=0;
 }else if(pst==3){
  if(b==0x07||b==0x9C)pst=0;
  else if(b==0x1B)pst=4;
 }else if(pst==4){pst=0;}
 else if(pst==5){pst=0;}}

static int log_fd=-1;
static void*reader(void*_){
 char buf[4096];
 while(run_){
  int n=(int)read(pty_fd,buf,sizeof(buf));
  if(n<=0)break;
  if(log_fd>=0)(void)!write(log_fd,buf,(size_t)n);
  for(int i=0;i<n;i++)feed((uint8_t)buf[i]);
  dirty=1;}
 return NULL;}
static void*inp_thr(void*_){
 char p[512],b[4096];
 snprintf(p,512,"%s/.inp",getenv("HOME")?getenv("HOME"):"/tmp");
 unlink(p);mkfifo(p,0600);
 while(run_){int f=open(p,O_RDONLY);if(f<0){usleep(100000);continue;}
  int n;while((n=(int)read(f,b,4096))>0)(void)!write(pty_fd,b,(size_t)n);close(f);}
 return NULL;}

static int kbh(float x,float y){for(int i=0;i<nkeys;i++)if(x>=KB[i][0]&&x<KB[i][2]&&y>=KB[i][1]&&y<KB[i][3])return i;return -1;}
static char kch(int idx){int c=0;for(int r=0;r<NROWS;r++){int n=(int)strlen(KR[r]);if(idx<c+n)return KR[r][idx-c];c+=n;}return 0;}
static void compute_kb(void){
 float kh=H*0.40f,ky=H-kh,rh=kh/NROWS;nkeys=0;
 for(int r=0;r<NROWS;r++){
  int n=(int)strlen(KR[r]);float y=ky+r*rh,kw=W/10.0f;
  for(int i=0;i<n;i++){
   float x,w;
   if(r==0){kw=W/(float)n;x=i*kw;w=kw;}
   else if(r==3){x=kw*0.5f+i*kw;w=kw;}
   else if(r==5){float pos[]={0,kw,2*kw,9*kw},wd[]={kw,kw,7*kw,W-9*kw};x=pos[i];w=wd[i];}
   else{x=i*kw;w=kw;}
   KB[nkeys][0]=x;KB[nkeys][1]=y;KB[nkeys][2]=x+w;KB[nkeys][3]=y+rh;nkeys++;}}}
static void compute_grid(void){
 if(!FN.cw||!FN.ch){rows=24;cols=80;return;}
 int kh=(int)(H*0.40f);
 int avail_h=H-kh;
 rows=avail_h/FN.ch;if(rows<1)rows=1;if(rows>MR)rows=MR;
 cols=W/FN.cw;if(cols<20)cols=20;if(cols>MC)cols=MC;
 if(cx>=cols)cx=cols-1;if(cy>=rows)cy=rows-1;
 for(int r=0;r<rows;r++)for(int c=0;c<cols;c++){if(G[r*MC+c].ch==0)G[r*MC+c]=(Cell){' ',7,0,0};}
 if(pty_fd>=0){struct winsize ws={(unsigned short)rows,(unsigned short)cols,0,0};ioctl(pty_fd,TIOCSWINSZ,&ws);}}

static inline void drawch(uint32_t*p,int stride,Font*f,int x,int y,int ch,uint32_t col,uint32_t bg){
 int ci=ch-32;
 uint32_t cr=(col>>16)&0xFF,cg=(col>>8)&0xFF,cb=col&0xFF;
 for(int dy=0;dy<f->ch;dy++){int fy=y+dy;if((unsigned)fy>=(unsigned)H)continue;
  for(int dx=0;dx<f->cw;dx++){int fx=x+dx;if((unsigned)fx>=(unsigned)W)continue;
   uint32_t out=bg;
   if(ci>=0&&ci<NC&&f->px){int sx=ci*f->cw;uint32_t a=f->px[dy*f->aw+sx+dx]>>24;
    if(a){out=0xFF000000|((a*cr/255)<<16)|((a*cg/255)<<8)|(a*cb/255);}}
   p[fy*stride+fx]=out;}}}
static void drawstr(uint32_t*p,int stride,Font*f,const char*s,int x,int y,uint32_t col){
 for(;*s;s++,x+=f->cw)drawch(p,stride,f,x,y,*s,col,0xFF000000);}

static const char*lbl(char c){
 switch((uint8_t)c){
 case 1:return "ESC";case 2:return "TAB";case 3:return "CTL";
 case 4:return "<";case 5:return "v";case 6:return "^";case 7:return ">";
 case 8:return "BS";case 9:return "EN";default:{static char b[2];b[0]=c;b[1]=0;return b;}}}

#define JF(ret,name) JNIEXPORT ret JNICALL Java_com_tlt_M_##name
JF(void,nResize)(JNIEnv*e,jclass c,jint w,jint hh){(void)e;(void)c;W=w;H=hh;compute_kb();compute_grid();dirty=1;}
JF(void,nFont)(JNIEnv*e,jclass c,jintArray arr){(void)c;
 jint*d=(*e)->GetIntArrayElements(e,arr,NULL);
 FN.cw=d[0];FN.ch=d[1];FN.aw=FN.cw*NC;
 int sz=FN.aw*FN.ch;free(FN.px);FN.px=malloc((size_t)sz*4);memcpy(FN.px,d+2,(size_t)sz*4);
 (*e)->ReleaseIntArrayElements(e,arr,d,0);compute_grid();dirty=1;}

JF(void,nRender)(JNIEnv*e,jclass c,jintArray arr){(void)c;
 jint*p=(*e)->GetIntArrayElements(e,arr,NULL);int stride=W;
 for(int i=0;i<W*H;i++)p[i]=0xFF000000;
 if(FN.px){
  int gridh=rows*FN.ch;
  for(int r=0;r<rows;r++)for(int col=0;col<cols;col++){
   Cell cl=G[r*MC+col];
   uint32_t fg=PAL[(cl.at&1)?(cl.fg|8)&15:cl.fg&15],bg=PAL[cl.bg&15];
   int x=col*FN.cw,y=r*FN.ch;
   int iscursor=(r==cy&&col==cx);
   uint32_t abg=iscursor?0xFF44FFAA:bg;
   uint32_t afg=iscursor?0xFF000000:fg;
   drawch((uint32_t*)p,stride,&FN,x,y,cl.ch?cl.ch:' ',afg,abg);
  }
  for(int i=0;i<nkeys;i++){
   int ix=(int)KB[i][0],iy=(int)KB[i][1],iw=(int)(KB[i][2]-KB[i][0]),ih=(int)(KB[i][3]-KB[i][1]);
   const char*lb=lbl(kch(i));int lw=(int)strlen(lb)*FN.cw;
   uint32_t col=(i==pressed)?0xFFFF3333:(ctrl_stk&&lb[0]=='C'&&lb[1]=='T')?0xFFFF8844:0xFFFFFFFF;
   int tx=ix+(iw-lw)/2,ty=iy+(ih-FN.ch)/2;
   drawstr((uint32_t*)p,stride,&FN,lb,tx,ty,col);
  }
  if(gridh<H-(int)(H*0.40f)){/*ok*/}
 }
 (*e)->ReleaseIntArrayElements(e,arr,p,0);dirty=0;}

static void pty_w(const char*s,int n){if(pty_fd>=0)(void)!write(pty_fd,s,n);}
static void send_key(int kidx){
 char c=kch(kidx);uint8_t b;
 switch((uint8_t)c){
 case 1:pty_w("\x1B",1);break;
 case 2:pty_w("\t",1);break;
 case 3:ctrl_stk=!ctrl_stk;dirty=1;return;
 case 4:pty_w("\x1B[D",3);break;
 case 5:pty_w("\x1B[B",3);break;
 case 6:pty_w("\x1B[A",3);break;
 case 7:pty_w("\x1B[C",3);break;
 case 8:pty_w("\x7F",1);break;
 case 9:pty_w("\r",1);break;
 default:
  if(ctrl_stk&&((c>='a'&&c<='z')||(c>='A'&&c<='Z'))){b=(uint8_t)((c|32)-'a'+1);pty_w((char*)&b,1);ctrl_stk=0;dirty=1;}
  else if(ctrl_stk&&c>='@'&&c<='_'){b=(uint8_t)(c-'@');pty_w((char*)&b,1);ctrl_stk=0;dirty=1;}
  else pty_w(&c,1);
 }}

JF(void,nTouch)(JNIEnv*e,jclass c,jint act,jfloat x,jfloat y){(void)e;(void)c;
 switch(act){
 case 0:pressed=kbh(x,y);if(pressed>=0){send_key(pressed);dirty=1;}break;
 case 1:case 3:pressed=-1;dirty=1;break;
 default:break;}}

JF(void,nStart)(JNIEnv*e,jclass c,jstring libDir,jstring filesDir){(void)c;
 if(pty_fd>=0)return;
 const char*ld=(*e)->GetStringUTFChars(e,libDir,NULL);
 const char*fd=(*e)->GetStringUTFChars(e,filesDir,NULL);
 char path[1024],ti[512],home[512],bin[512];
 snprintf(bin,sizeof(bin),"%s/bin",fd);
 snprintf(path,sizeof(path),"%s:%s:/system/bin:/system/xbin:/product/bin:/apex/com.android.runtime/bin",bin,ld);
 snprintf(ti,sizeof(ti),"%s/terminfo",fd);
 snprintf(home,sizeof(home),"%s",fd);
 (*e)->ReleaseStringUTFChars(e,libDir,ld);
 (*e)->ReleaseStringUTFChars(e,filesDir,fd);
 int m=posix_openpt(O_RDWR|O_NOCTTY);
 if(m<0){__android_log_print(ANDROID_LOG_ERROR,"tlt","openpt:%s",strerror(errno));return;}
 grantpt(m);unlockpt(m);
 char sn[64];if(ptsname_r(m,sn,sizeof(sn))!=0){close(m);return;}
 pid_t p=fork();
 if(p==0){setsid();int sfd=open(sn,O_RDWR);if(sfd<0)_exit(127);
  dup2(sfd,0);dup2(sfd,1);dup2(sfd,2);if(sfd>2)close(sfd);close(m);
  ioctl(0,TIOCSCTTY,0);
  setenv("TERM","xterm-256color",1);
  setenv("TERMINFO",ti,1);
  setenv("HOME",home,1);
  setenv("PATH",path,1);
  setenv("TMPDIR",home,1);
  setenv("TMUX_TMPDIR",home,1);
  setenv("A_SDIR",home,1);
  {char rc[600];snprintf(rc,600,"%s/rclone.conf",home);setenv("RCLONE_CONFIG",rc,1);}
  chdir(home);
  setenv("PS1","$ ",1);
  execl("/system/bin/sh","sh",(char*)NULL);_exit(127);}
 pty_fd=m;run_=1;setenv("HOME",home,1);
 {char lp[512];snprintf(lp,512,"%s/.log",home);log_fd=open(lp,O_WRONLY|O_CREAT|O_TRUNC,0600);}
 for(int r=0;r<MR;r++)for(int cc=0;cc<MC;cc++)G[r*MC+cc]=(Cell){' ',7,0,0};
 cx=cy=0;cfg=7;cbg=0;cat=0;pst=0;
 compute_grid();
 pthread_create(&rt,NULL,reader,NULL);
 {pthread_t it;pthread_create(&it,NULL,inp_thr,NULL);pthread_detach(it);}}
JF(void,nStop)(JNIEnv*e,jclass c){(void)e;(void)c;run_=0;if(pty_fd>=0){close(pty_fd);pty_fd=-1;}}
JF(jboolean,nDirty)(JNIEnv*e,jclass c){(void)e;(void)c;return dirty?1:0;}
'''

MF='''<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
<uses-permission android:name="android.permission.INTERNET"/>
<application android:label="tlt" android:extractNativeLibs="true" android:theme="@android:style/Theme.NoTitleBar.Fullscreen">
<activity android:name=".M" android:exported="true" android:launchMode="singleTask" android:configChanges="orientation|screenSize|keyboardHidden">
<intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
</activity></application></manifest>
'''

GB='''plugins{id("com.android.application");id("org.jetbrains.kotlin.android")}
android{namespace="com.tlt";compileSdk=34
defaultConfig{applicationId="com.tlt";minSdk=26;targetSdk=34;versionCode=1;versionName="0.1"
ndk{abiFilters+=listOf("arm64-v8a")}}
packaging{jniLibs{useLegacyPackaging=true}}
compileOptions{sourceCompatibility=JavaVersion.VERSION_17;targetCompatibility=JavaVersion.VERSION_17}
kotlinOptions{jvmTarget="17"}}
'''

GS='''pluginManagement{repositories{google();mavenCentral();gradlePluginPortal()}
plugins{id("com.android.application") version "8.7.3";id("org.jetbrains.kotlin.android") version "1.9.25"}}
dependencyResolutionManagement{repositories{google();mavenCentral()}}
include(":app")
'''

GP='''android.useAndroidX=true
org.gradle.jvmargs=-Xmx4g
'''

def wr(p,s):
 os.makedirs(os.path.dirname(p),exist_ok=True)
 open(p,"w").write(s)

def main():
 wr(D+"/settings.gradle.kts",GS);wr(D+"/app/build.gradle.kts",GB)
 wr(D+"/gradle.properties",GP);wr(D+"/local.properties",f"sdk.dir={SDK}\n")
 wr(D+"/app/src/main/AndroidManifest.xml",MF)
 wr(D+"/app/src/main/java/com/tlt/M.kt",KT)
 cfile=D+"/cpp/t.c";wr(cfile,CSRC)
 jl=D+"/app/src/main/jniLibs/arm64-v8a";os.makedirs(jl,exist_ok=True)
 sof=jl+"/libt.so"
 print("> compile libt.so")
 S.check_call([CC,"-shared","-O2","-fPIC","-Wall","-w","-Wl,-z,max-page-size=16384","-o",sof,cfile,"-landroid","-llog"])
 # Bundle tmux + tic as native libs (Android exec-allowed SELinux context)
 as_dir=D+"/app/src/main/assets";os.makedirs(as_dir,exist_ok=True)
 tmux_src="/tmp/dsrc/tmux-3.4/tmux";tic_src="/tmp/dsrc/ncurses-6.4/progs/tic";ssh_src="/tmp/dsrc/dropbear-2024.86/dbclient"
 NDK=sorted([d for d in os.listdir(H+"/Library/Android/sdk/ndk")])[-1]
 ACC=f"{H}/Library/Android/sdk/ndk/{NDK}/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang"
 AST=f"{H}/Library/Android/sdk/ndk/{NDK}/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
 SRC=H+"/a";a_out="/tmp/a_aarch64"
 S.check_call([ACC,"-w","-O2","-DSRC=\""+SRC+"\"","-o",a_out,SRC+"/a.c"])
 S.check_call([AST,a_out])
 rc_src="/tmp/rc/rclone"
 for src,dst in [(tmux_src,jl+"/libtmux.so"),(tic_src,jl+"/libtic.so"),(ssh_src,jl+"/libssh.so"),(a_out,jl+"/libaa.so"),(rc_src,jl+"/librclone.so")]:
  if os.path.exists(src):shutil.copy(src,dst)
  else:print(f"! missing {src}")
 for n in ["xterm-256color","screen","screen-256color"]:
  s="/tmp/dsrc/"+n
  if os.path.exists(s):shutil.copy(s,as_dir+"/"+n)
 fnt="/tmp/JetBrainsMono-Regular.ttf"
 if os.path.exists(fnt):shutil.copy(fnt,as_dir+"/mono.ttf")
 wrap_c="/tmp/ssh_wrap.c"
 open(wrap_c,"w").write(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){
 const char*b=strrchr(argv[0],'/');b=b?b+1:argv[0];
 if(!strcmp(b,"sshpass")){
  if(argc>=3&&!strcmp(argv[1],"-p")){setenv("DROPBEAR_PASSWORD",argv[2],1);argv+=2;argc-=2;}
  execvp(argv[1],argv+1);perror(argv[1]);return 127;}
 /* ssh: translate openssh flags to dbclient */
 char*na[64]={"dbclient","-y","-y"};int n=3;
 int i=1;while(i<argc){char*a=argv[i];
  if(!strcmp(a,"-o")){i+=2;continue;}
  if(a[0]=='-'&&a[1]=='o'){i++;continue;}
  if(!strcmp(a,"-p")&&i+1<argc){na[n++]="-p";na[n++]=argv[i+1];i+=2;continue;}
  if(!strncmp(a,"-p",2)&&a[2]){na[n++]="-p";na[n++]=a+2;i++;continue;}
  if(!strcmp(a,"-i")&&i+1<argc){na[n++]="-i";na[n++]=argv[i+1];i+=2;continue;}
  if(!strcmp(a,"-tt")||!strcmp(a,"-t")||!strcmp(a,"-T")||!strcmp(a,"-N")||!strcmp(a,"-f")||!strcmp(a,"-q")||!strcmp(a,"-A")||!strcmp(a,"-g")||!strcmp(a,"-z")||!strcmp(a,"-4")||!strcmp(a,"-6")||!strcmp(a,"-C")||!strcmp(a,"-v")||!strcmp(a,"-V")){na[n++]=a;i++;continue;}
  break;}
 while(i<argc&&n<63)na[n++]=argv[i++];
 na[n]=NULL;execvp("dbclient",na);perror("dbclient");return 127;
}
''')
 wrap_out="/tmp/ssh_wrap"
 S.check_call([ACC,"-w","-O2","-o",wrap_out,wrap_c])
 S.check_call([AST,wrap_out])
 shutil.copy(wrap_out,jl+"/libsshwrap.so")
 TL=H+"/androidDev/apps/text-launcher"
 for f in ["gradlew","gradle/wrapper/gradle-wrapper.jar","gradle/wrapper/gradle-wrapper.properties"]:
  src,dst=TL+"/"+f,D+"/"+f
  if os.path.exists(src):
   os.makedirs(os.path.dirname(dst),exist_ok=True);shutil.copy(src,dst)
 if not os.path.exists(D+"/gradlew"):sys.exit("x no gradlew source")
 os.chmod(D+"/gradlew",0o755)
 print("> gradle assembleDebug")
 os.chdir(D)
 S.check_call(["./gradlew","--no-daemon","--no-configuration-cache","assembleDebug"])
 apks=glob.glob(D+"/app/build/outputs/apk/debug/*.apk")
 if not apks:sys.exit("x no APK")
 apk=apks[0];print(f"+ APK: {apk}")
 if len(sys.argv)>1 and sys.argv[1]=="build":return
 devs=[l.split('\t')[0] for l in S.run(["adb","devices"],capture_output=True,text=True).stdout.splitlines()[1:] if '\tdevice' in l]
 if not devs:sys.exit("x no adb device")
 ser=devs[0]
 r=S.run(["adb","-s",ser,"install","-r",apk],capture_output=True,text=True)
 if "INSTALL_FAILED" in r.stdout+r.stderr:
  S.run(["adb","-s",ser,"uninstall",PKG],capture_output=True)
  r=S.run(["adb","-s",ser,"install",apk],capture_output=True,text=True)
 if r.returncode:sys.exit(r.stderr or r.stdout)
 S.run(["adb","-s",ser,"shell","am","start","-n",f"{PKG}/.M"])
 print(f"+ launched on {ser}")

main()
