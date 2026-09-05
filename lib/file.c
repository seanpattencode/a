/* file — recent Downloads: open or move to cwd */
typedef struct{char p[P];time_t t;}gf_t;
static int gf_cmp(const void*a,const void*b){return((const gf_t*)b)->t>((const gf_t*)a)->t?1:-1;}
static int cmd_get(int c,char**v){perf_disarm();
    char dl[P];snprintf(dl,P,"%s/Downloads",HOME);gf_t fs[512]={0};int nf=0;struct stat st;
    DIR*d=opendir(dl);struct dirent*e;if(d){while((e=readdir(d))&&nf<512){if(e->d_name[0]=='.')continue;
        snprintf(fs[nf].p,P,"%s/%s",dl,e->d_name);if(!stat(fs[nf].p,&st)){fs[nf].t=st.st_mtime;nf++;}}closedir(d);}
    if(!nf){puts("Downloads empty");return 1;}
    qsort(fs,(size_t)nf,sizeof(gf_t),gf_cmp);int sh=nf>20?20:nf,pk=-1;
    if(c>2){char q[B]="";ajoin(q,B,c,v,2);for(int i=0;i<nf;i++)if(strcasestr(fs[i].p,q)){pk=i;break;}}
    if(pk<0){for(int i=0;i<sh;i++){char*n=strrchr(fs[i].p,'/');printf("  %d. %s\n",i+1,n?n+1:fs[i].p);}
        printf("[1-%d]: ",sh);fflush(stdout);char b[8];if(!fgets(b,8,stdin))return 1;pk=atoi(b)-1;if(pk<0||pk>=nf)return 1;}
    char*s=fs[pk].p,*fn=strrchr(s,'/');fn=fn?fn+1:s;
    printf("[o]pen [m]ove: ");fflush(stdout);char a[4];if(!fgets(a,4,stdin))return 1;
    if(*a=='o'){bg_exec(OPENER,s);puts(fn);return 0;}
    CWD(wd);char dst[P];snprintf(dst,P,"%s/%s",wd,fn);
    if(rename(s,dst)){perror("mv");return 1;}printf("✓ → %s\n",wd);return 0;}
