static int cmd_project_num(int argc, char **argv, int idx) { (void)argc; (void)argv;
    init_db(); load_cfg(); load_proj(); load_apps();
    if (idx >= 0 && idx < NPJ) {
        proj_t *p=&PJ[idx]; char c[B];
        if (!dexists(p->path) && p->gdrive[0]) {
            printf("gdrive: %s\n",p->gdrive);
            mkdirp(p->path);
            snprintf(c,B,"(rclone lsf '%s' --dirs-only -q | while IFS= read -r d; do mkdir -p '%s/'\"$d\"; done; rclone copy '%s' '%s' --max-depth 1 -q)&",p->gdrive,p->path,p->gdrive,p->path);
            (void)!system(c);
        } else if (!dexists(p->path) && p->repo[0]) {
            printf("Cloning %s...\n",p->repo);snprintf(c,B,"git clone '%s' '%s'",p->repo,p->path);(void)!system(c);
        }
        if (!dexists(p->path)) { printf("x %s\n", p->path); return 1; }
        if (getenv("A_TUI")) { unsetenv("A_TUI"); (void)!chdir(p->path);
            int t=open("/dev/tty",O_WRONLY);if(t>=0){dup2(t,2);close(t);}
            const char*sh=getenv("SHELL");execlp(sh?sh:"bash",sh?sh:"bash","-i",(char*)NULL);}
        snprintf(c,B,"%s/cd_target",DDIR); writef(c,p->path); printf("%s\n",p->path);
        if(p->gdrive[0]) return 0; /* gdrive project: skip git/ghost */
        if(!fork()){snprintf(c,B,"git -C '%s' ls-remote --exit-code origin HEAD>/dev/null 2>&1&&mkdir -p '%s/logs'&&touch '%s/logs/push.ok'",p->path,DDIR,DDIR);(void)!system(c);_exit(0);}
        /* ghost: pre-spawn default agent */
        if(!fork()){setsid();load_sess();
            const char*dk=cfget("default_agent");if(!dk[0])dk="c";
            sess_t*gs=find_sess(dk);if(!gs)_exit(0);
            char sn[256];snprintf(sn,256,"%s-%s",gs->name,p->name);
            if(tm_has(sn))_exit(0);
            char gf[P];snprintf(gf,P,"%s/ghost",DDIR);
            char*og=readf(gf,NULL);if(og){og[strcspn(og,"\n")]=0;
                char k[B];snprintf(k,B,"tmux kill-session -t '=%s' 2>/dev/null",og);(void)!system(k);free(og);}
            tm_ensure_conf();create_sess(sn,p->path,gs->cmd,NULL);writef(gf,sn);
            if(!fork()){sleep(30);char*g=readf(gf,NULL);
                if(g){g[strcspn(g,"\n")]=0;if(!strcmp(g,sn)){
                    char k[B];snprintf(k,B,"tmux kill-session -t '=%s' 2>/dev/null",sn);(void)!system(k);unlink(gf);}free(g);}
                _exit(0);}
            _exit(0);}
        return 0;
    }
    int ai = idx - NPJ;
    if (ai>=0 && ai<NAP) { char ex[B],*p,*e; strcpy(ex,AP[ai].cmd); while((p=strchr(ex,'{'))&&(e=strchr(p,'}'))){ *e=0;for(int j=0;j<NPJ;j++)if(!strcmp(PJ[j].name,p+1)){*p=0;char t[B];sprintf(t,"%s%s%s",ex,PJ[j].path,e+1);strcpy(ex,t);break;}} if((p=strstr(ex,"python "))){memmove(p+7,p+6,strlen(p+6)+1);p[6]='3';} printf("> %s\n",AP[ai].name); return system(ex)>>8; }
    printf("x Invalid index: %d\n", idx); return 1;
}

static int cmd_setup(int argc, char **argv) { (void)argc; (void)argv;
    char cwd[P]; if (!getcwd(cwd, P)) strcpy(cwd, ".");
    if (git_in_repo(cwd)) { puts("x Already a git repo"); return 1; }
    char c[B]; snprintf(c, B, "cd '%s' && git init && git add -A && git commit -m 'init' && gh repo create '%s' --private --source . --push", cwd, bname(cwd));
    return system(c) >> 8;
}
