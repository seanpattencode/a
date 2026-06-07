static int cmd_pow(int c,char**v){
    const char*u="pow o power off\npow r restart\npow s suspend\npow h hibernate";
    if(c<3){puts(u);return 0;}
    char k=(!strncmp(v[2],"sh",2)||*v[2]=='d')?'o':*v[2];
    const char*l="orsh",*p=strchr(l,k),*x[]={"poweroff","reboot","suspend","hibernate"};
    if(!p){puts(u);return 1;}
#ifdef __APPLE__
    if(k=='o'||k=='r')execlp("osascript","osascript","-e",k=='o'?"tell app \"System Events\" to shut down":"tell app \"System Events\" to restart",(char*)0);
    if(k=='s')execlp("pmset","pmset","sleepnow",(char*)0);
    puts("x hibernate unsupported");return 1;
#else
    execlp("sudo","sudo","systemctl",x[p-l],(char*)0);
#endif
    perror("pow");return 1;
}
