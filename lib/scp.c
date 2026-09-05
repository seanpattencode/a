/* scp - tui pick file→host→dir, transfer */
static char*spk(const char*pr,char*raw){
    static char*it[256];int n=0;
    for(char*p=raw;*p&&n<256;){char*nl=strchr(p,'\n');if(nl)*nl=0;if(*p)it[n++]=p;if(!nl)break;p=nl+1;}
    struct termios o,r;tcgetattr(0,&o);r=o;r.c_lflag&=~(tcflag_t)(ICANON|ECHO|ISIG);tcsetattr(0,TCSANOW,&r);
    char b[128]="";int bl=0,s=0,nm=0,ix[256];
    for(;;){nm=0;for(int i=0;i<n&&nm<256;i++)if(!bl||strcasestr(it[i],b))ix[nm++]=i;
        if(s>=nm)s=nm?nm-1:0;
        printf("\033[H\033[J%s> %s\n",pr,b);
        for(int i=0;i<nm&&i<20;i++)printf("%c %s\n",i==s?'>':' ',it[ix[i]]);
        char c;if(read(0,&c,1)!=1)break;
        if(c=='\t'){if(++s>=nm)s=0;}
        else if(c=='\r'||c=='\n')break;
        else if(c==127){if(bl)b[--bl]=0;s=0;}
        else if(c==3){nm=0;break;}
        else if(c>=' '&&bl<127){b[bl++]=c;b[bl]=0;s=0;}}
    tcsetattr(0,TCSANOW,&o);
    return nm?it[ix[s]]:NULL;}
static int cmd_scp(int argc,char**argv){(void)argc;(void)argv;AB;perf_disarm();
    if(!isatty(0)){puts("a scp: needs a terminal");return 1;}   /* headless took item 0 of each = blind send */
    char fb[B*4]="",hb[B*4]="",rb[B*4]="",qc[B*2];
    pcmd("ls -p|grep -v /",fb,B*4);
    char*ff=spk("file",fb);if(!ff)return 1;
    snprintf(qc,B*2,"ls %s/ssh/*.txt 2>/dev/null|sed 's|.*/||;s/.txt$//'",SROOT);pcmd(qc,hb,B*4);
    char*hn=spk("host",hb);if(!hn)return 1;
    char hf[P];snprintf(hf,P,"%s/ssh/%s.txt",SROOT,hn);
    kvs_t kv=kvfile(hf);char hp[256],port[8];ssh_parse(kvget(&kv,"Host"),hp,port);
    snprintf(qc,B*2,"ssh -p %s '%s' 'ls -d ~ ~/*/ 2>/dev/null'",port,hp);pcmd(qc,rb,B*4);
    char*dn=spk("remote dir",rb);if(!dn)return 1;
    snprintf(qc,B*2,"scp -P %s '%s' '%s:%s'&&ssh -tt -p %s '%s' 'cd %s;ls;bash -l'",port,ff,hp,dn,port,hp,dn);
    execl("/bin/sh","sh","-c",qc,(char*)0);return 1;}
