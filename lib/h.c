/* h/home: primary homebox, else most-used host */
static int cmd_h(int c,char**v){(void)c;(void)v;perf_disarm();char p[P],s[128],h[128]="homebox";
    snprintf(p,P,"%s/ssh/homebox.txt",SROOT);
    if(!fexists(p)){*h=0;snprintf(p,P,"%s/freq_cache.txt",DDIR);FILE*f=fopen(p,"r");
        while(f&&!*h&&fgets(s,128,f))if(sscanf(s,"ssh %127[^:]",h)==1){snprintf(p,P,"%s/ssh/%s.txt",SROOT,h);if(!fexists(p))*h=0;}
        if(f)fclose(f);}
    execlp("a","a","ssh",*h?h:NULL,(char*)0);return 1;}
