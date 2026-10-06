static int in_fork(const char *p) { return strstr(p, "/adata/forks/") != NULL; }
static int fork_cp(const char *src, const char *dst) {
    char c[B]; snprintf(c,B,"mkdir '%s'&&(cd '%s'&&git ls-files -z|tar -cf - --null -T -|tar -xf - -C '%s')&&rm -rf '%s/adata'&&ln -sf '%s' '%s/adata'",dst,src,dst,dst,AROOT,dst);
    int r=system(c);
    if(!r){char rem[512]="",rc[B];snprintf(rc,B,"git -C '%s' remote get-url origin 2>/dev/null",src);
        pcmd(rc,rem,512);rem[strcspn(rem,"\n")]=0;
        if(rem[0]){char rf[P];snprintf(rf,P,"%s/.fork_remote",dst);writef(rf,rem);}}
    return r;
}

static int cmd_fork(int argc, char **argv) {
    char fd[P]; snprintf(fd,P,"%s/forks",AROOT); mkdirp(fd);
    const char *sub=argc>2?argv[2]:NULL;

    if(!sub||!strcmp(sub,"ls")) {
        char c[B]; snprintf(c,B,"ls -d '%s'/*/ 2>/dev/null",fd);
        FILE*f=popen(c,"r"); char ln[P]; int n=0;
        if(f){while(fgets(ln,P,f)){ln[strcspn(ln,"\n")]=0;
            if(ln[0]){int l=(int)strlen(ln);if(l>1&&ln[l-1]=='/')ln[l-1]=0;}
            char*s=strrchr(ln,'/');printf("  %s\n",s?s+1:"?");n++;}pclose(f);}
        if(!n)puts("no forks");
        return 0;
    }

    if(!strcmp(sub,"rm")) {
        if(argc<4){fprintf(stderr,"Usage: a fork rm <name>\n");return 1;}
        char fp[P]; snprintf(fp,P,"%s/%s",fd,argv[3]);
        struct stat st; if(stat(fp,&st)){fprintf(stderr,"x %s?\n",argv[3]);return 1;}
        char c[B]; snprintf(c,B,"rm -rf '%s'",fp); (void)!system(c);
        printf("✓ rm %s\n",argv[3]); return 0;
    }

    if(!strcmp(sub,"run")) {
        if(argc<5){fprintf(stderr,"Usage: a fork run <name> <cmd...>\n");return 1;}
        char fp[P]; snprintf(fp,P,"%s/%s",fd,argv[3]);
        struct stat st; if(stat(fp,&st)){fprintf(stderr,"x %s?\n",argv[3]);return 1;}
        if(chdir(fp)){perror(fp);return 1;}
        execvp(argv[4],argv+4);perror(argv[4]);return 1;
    }

    /* a fork <name> — lightweight copy, no .git */
    char fp[P]; snprintf(fp,P,"%s/%s",fd,sub);
    struct stat st; if(!stat(fp,&st)){printf("exists: %s\n",fp);
        char tf[P];snprintf(tf,P,"%s/cd_target",DDIR);writef(tf,fp);return 0;}
    if(fork_cp(SDIR,fp)){fprintf(stderr,"x copy failed\n");return 1;}
    printf("✓ %s\n",fp);
    char tf[P];snprintf(tf,P,"%s/cd_target",DDIR);writef(tf,fp);
    return 0;
}
