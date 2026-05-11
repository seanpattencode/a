static int cmd_m(int c,char**v){
    if(c>2&&!strcmp(v[2],"a")){
        char tf[]="/tmp/aboxXXXXXX";int fd=mkstemp(tf);if(fd<0)return 1;close(fd);
        pid_t p=fork();
        if(p==0){execlp("e","e","--box",c>3?v[3]:"input:",tf,(char*)0);_exit(127);}
        int st;waitpid(p,&st,0);
        char*body=readf(tf,NULL);if(body){dprintf(1,"%s",body);free(body);}
        unlink(tf);return 0;
    }
    char b[B];snprintf(b,B,"cd '%s'&&gh repo create m --private --clone",AROOT);return system(b);
}
