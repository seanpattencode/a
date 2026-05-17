/* a relay [rport] — reverse-tunnel :22 to ubuntuSSD4Tb-wan:rport, write <DEV>-relay.txt for `a ssh <DEV>` fallback */
static int cmd_relay(int argc,char**argv){
    perf_disarm();int rp=argc>2?atoi(argv[2]):2201;
    char p[P],c[B],rd[B],opts[256],hp[256],port[8];
    snprintf(p,P,"%s/ssh/ubuntuSSD4Tb-wan.txt",SROOT);
    kvs_t kv=kvfile(p);const char*h=kvget(&kv,"Host"),*pw=kvget(&kv,"Password");
    if(!h){puts("x no ubuntuSSD4Tb-wan");return 1;}
    ssh_parse(h,hp,port);
    snprintf(p,P,"%s/ssh/%s.txt",SROOT,DEV);
    kvs_t k2=kvfile(p);const char*opw=kvget(&k2,"Password");
    snprintf(p,P,"%s/ssh/%s-relay.txt",SROOT,DEV);
    snprintf(rd,B,"Name: %s-relay\nHost: seanpatten@127.0.0.1:%d\nPassword: %s\nJump: %s\nJumpPw: %s\n",DEV,rp,opw?opw:"",h,pw?pw:"");
    writef(p,rd);
    snprintf(opts,256,"-N -R %d:localhost:22 -oExitOnForwardFailure=yes -oServerAliveInterval=30 -oServerAliveCountMax=3 -oStrictHostKeyChecking=accept-new",rp);
    ssh_pre(c,B,pw,opts,port,hp);
    printf("+ %s:%d (saved %s-relay)\n",hp,rp,DEV);
    execl("/bin/sh","sh","-c",c,(char*)NULL);_exit(127);
}
