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
        if(p->gdrive[0]) return 0; /* gdrive: skip git housekeeping */
        if(!fork()){snprintf(c,B,"git -C '%s' ls-remote --exit-code origin HEAD>/dev/null 2>&1&&mkdir -p '%s/logs'&&touch '%s/logs/push.ok'",p->path,DDIR,DDIR);(void)!system(c);_exit(0);}
        /* nav only: no agent pre-spawn. the old "ghost" split claude into the user's pane
           (tm_new splits when $TMUX is set) and was never claimed anyway — cmd_sess looks for a
           time-suffixed name the ghost lacked — so it only ever surfaced as an unwanted window. */
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
