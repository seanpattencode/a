static int cmd_w(int c,char**v){AB;init_paths();
    char f[P],*hl[32],*ht[32]={0};int n=0,dl=snprintf(f,P,"%s/workcycle",SROOT);mkdirp(f);
    strcpy(f+dl,"/habits.txt");
    if(!fexists(f)){FILE*o=fopen(f,"a");if(o){fputs("garlic\nexercise\nnoot\neggs\ncold_shower\n",o);fclose(o);}}
    char*h=readf(f,NULL);if(!h)return 1;
    for(char*l=h;*l&&n<32;l++){char*p=strchr(l,'\n');if(p)*p=0;if(*l){char*at=strchr(l,'@');if(at){*at=0;ht[n]=at+1;}hl[n++]=l;}l=p?p:l+strlen(l);}
    char ds[12];strftime(ds,12,"%F",localtime(&(time_t){time(0)}));
    snprintf(f+dl,(size_t)(P-dl),"/%s_%s.txt",ds,DEV);
    char*dn=readf(f,NULL);
    if(c>2&&!strcmp(v[2],"check")){perf_disarm();char wf[P];snprintf(wf,P,"%s/local/wcheck_%s.txt",AROOT,ds);char*wd=readf(wf,NULL);
        struct tm*T=localtime(&(time_t){time(0)});int now=T->tm_hour*60+T->tm_min,hh,mm;char cm[B];
        for(int i=0;i<n;i++)if(ht[i]&&sscanf(ht[i],"%d:%d",&hh,&mm)==2&&hh*60+mm<=now&&!(dn&&strstr(dn,hl[i]))&&!(wd&&strstr(wd,hl[i]))){
            snprintf(cm,B,"a email 'missed: %s' 'expected by %s'",hl[i],ht[i]);(void)!system(cm);
            FILE*o=fopen(wf,"a");if(o){fprintf(o,"%s\n",hl[i]);fclose(o);}}
        free(wd);free(h);free(dn);return 0;}
    if(c<3){for(int i=0;i<n;i++)printf(" %d.%s %s%s%s\n",i+1,dn&&strstr(dn,hl[i])?"✓":"[ ]",hl[i],ht[i]?"@":"",ht[i]?ht[i]:"");
        puts(" a w <#|name>");}
    else{int i=atoi(v[2]);char*s=i>0&&i<=n?hl[i-1]:v[2];
        {FILE*o=fopen(f,"a");if(o){fputs(s,o);fputc('\n',o);fclose(o);}}
        printf("✓ %s\n",s);}free(h);free(dn);return 0;}
