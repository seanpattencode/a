static int cmd_pow(int c,char**v){
    const char*u="pow o power off\npow r restart\npow s suspend\npow h hibernate";
    if(c<3){puts(u);return 0;}
    char k=(!strncmp(v[2],"sh",2)||*v[2]=='d')?'o':*v[2];
    const char*l="orsh",*p=strchr(l,k);
    if(!p){puts(u);return 1;}
#ifdef __APPLE__
    if(k=='s')execlp("pmset","pmset","sleepnow",(char*)0);
    if(k>'h')execlp("osascript","osascript","-e",k=='o'?"tell app \"System Events\" to shut down":"tell app \"System Events\" to restart",(char*)0);
    puts("x unsupported");return 1;
#else
    execlp("sudo","sudo","systemctl",(const char*[]){"poweroff","reboot","suspend","hibernate"}[p-l],(char*)0);
#endif
    perror("pow");return 1;
}
