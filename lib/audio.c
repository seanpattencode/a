#define AU 16
static int au_ld(char N[][100],char D[][28],int*di){
    FILE*f=popen("pactl get-default-sink;pactl list sinks","r");if(!f)return 0;
    char e[100]="",l[128],n[100]="",d[28]="";int c=0;*di=-1;
    if(!fgets(e,100,f)){pclose(f);return 0;}e[strcspn(e,"\n")]=0;
    while(fgets(l,128,f)){char*p=l;while(*p==9||*p==32)p++;int o;
        if(!strncmp(p,"Sink ",5)){if(*n&&c<AU){snprintf(N[c],100,"%s",n);snprintf(D[c],28,"%s",*d?d:n);if(!strcmp(N[c],e))*di=c;c++;}*n=*d=0;}
        else if((o=!strncmp(p,"Name: ",6)?6:!strncmp(p,"Description: ",13)?13:0)){l[strcspn(l,"\n")]=0;snprintf(o==6?n:d,(size_t)(o==6?100:28),"%s",p+o);}}
    if(*n&&c<AU){snprintf(N[c],100,"%s",n);snprintf(D[c],28,"%s",*d?d:n);if(!strcmp(N[c],e))*di=c;c++;}
    return pclose(f),c;}
static int au_set(const char*n){return setenv("A_SINK",(char*)n,1),system("pactl set-default-sink \"$A_SINK\"&&pactl list short sink-inputs|cut -f1|xargs -r -I@ pactl move-sink-input @ \"$A_SINK\"");}
static int cmd_audio(int ac,char**av){
    perf_disarm();char N[AU][100],D[AU][28];int di=-1,n=au_ld(N,D,&di),i;if(!n)return puts("x no sinks"),1;
    const char*a=ac>2?av[2]:0;
    if(a&&strcmp(a,"ls")&&*a!='-'){
        if(!a[strspn(a,"0123456789")]){i=atoi(a)-1;if(i<0||i>=n)i=-1;}
        else{i=-1;int k=0;for(int j=0;j<n;j++)if(strcasestr(D[j],a)||strcasestr(N[j],a))i=j,k++;if(k!=1)i=-1;}
        if(i<0){printf("x %s\n",a);goto L;}return au_set(N[i])?1:(printf("→ %s\n",D[i]),0);}
    if(a||!isatty(0)||!isatty(1)){L:for(i=0;i<n;i++)printf("%s%d %s\n",i==di?"*":" ",i+1,D[i]);return 0;}
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);r.c_cc[VMIN]=1;tcsetattr(0,TCSANOW,&r);
    fputs("\033[?25l",stdout);char ft[16]="";int fl=0,cur=di>=0?di:0;struct timespec tk;clock_gettime(CLOCK_MONOTONIC,&tk);
    for(;;){int ix[AU],m=0;for(i=0;i<n;i++)if(!fl||strcasestr(D[i],ft)||strcasestr(N[i],ft))ix[m++]=i;
        if(cur>=m)cur=m?m-1:0;struct winsize w;ioctl(1,TIOCGWINSZ,&w);int R=w.ws_row>8?w.ws_row:24,t=R-n-2;if(t<1)t=1;
        char b[B];int l=snprintf(b,B,"\033[%d;1H",t);
        for(i=0;i<m;i++){int s=ix[i];l+=snprintf(b+l,(size_t)(B-l),"%s%c%d %-28.28s%s\033[K\n",i==cur?"\033[7m":"",s==di?'*':' ',s+1,D[s],i==cur?"\033[0m":"");}
        for(i=m;i<n;i++)l+=snprintf(b+l,(size_t)(B-l),"\033[K\n");
        double ms;{struct timespec x;clock_gettime(CLOCK_MONOTONIC,&x);ms=(x.tv_sec-tk.tv_sec)*1e3+(x.tv_nsec-tk.tv_nsec)/1e6;}
        l+=snprintf(b+l,(size_t)(B-l),"\033[%d;1H\033[90mjk↵ type q %s \033[37m%.4fms\033[0m\033[K",R,ft,ms);
        (void)!write(1,b,(size_t)l);unsigned char ch;if(read(0,&ch,1)!=1)break;clock_gettime(CLOCK_MONOTONIC,&tk);
        if(ch=='q'||ch==3||ch==27)break;
        if(ch=='j'){if(cur+1<m)cur++;continue;}if(ch=='k'){if(cur)cur--;continue;}
        if((ch==127||ch==8)&&fl){ft[--fl]=0;cur=0;continue;}
        if(ch=='\r'||ch=='\n'){if(!m)continue;int s=ix[cur],rc=au_set(N[s]);tcsetattr(0,TCSANOW,&o);
            return fputs("\033[?25h\033[H\033[J",stdout),printf(rc?"x\n":"→ %s\n",D[s]),!!rc;}
        if(ch>31&&ch<127&&fl<15)ft[fl++]=(char)ch,ft[fl]=0,cur=0;}
    tcsetattr(0,TCSANOW,&o);return fputs("\033[?25h\033[H\033[J",stdout),0;}
#undef AU
