/* a vm [run|ssh|kill|test] [debian|arch|fedora|android] — disposable QEMU VM */
static int vm_android(void){
    char sdk[P];const char*e=getenv("ANDROID_HOME");
    snprintf(sdk,P,e?"%s":"%s/Android/Sdk",e?e:HOME);
    char em[P];snprintf(em,P,"%s/emulator/emulator",sdk);
    if(!fexists(em)){puts("x install Android SDK emulator (sdkmanager)");return 1;}
    char c[B];snprintf(c,B,"%s -list-avds 2>/dev/null|head -1",em);
    char avd[64]={0};FILE*f=popen(c,"r");if(f){(void)!fgets(avd,64,f);pclose(f);}avd[strcspn(avd,"\n")]=0;
    if(!*avd){puts("x no AVD — run: avdmanager create avd");return 1;}
    snprintf(c,B,"%s -avd %s -no-window -no-audio -no-snapshot >/tmp/avd.log 2>&1 &",em,avd);system(c);
    printf("> booting %s...\n",avd);fflush(stdout);
    system("adb -s emulator-5554 wait-for-device shell 'until getprop sys.boot_completed|grep -q 1;do sleep 1;done' 2>/dev/null");
    execlp("adb","adb","-s","emulator-5554","shell","-t","cd /data/local/tmp;exec sh",(char*)0);return 1;
}
typedef struct{const char*name,*url,*user,*cloudinit;}vmos_t;
static const vmos_t VMOS[]={
    {"debian","https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2","debian",
     "#cloud-config\npassword: %s\nchpasswd: {expire: false}\nssh_pwauth: true\npackages: [git,curl,tmux,openssh-server]\nruncmd: [systemctl enable --now ssh]\n"},
    {"arch","https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2","arch",
     "#cloud-config\npassword: %s\nchpasswd: {expire: false}\nssh_pwauth: true\nruncmd: [systemctl enable --now sshd]\n"},
    {"fedora","https://download.fedoraproject.org/pub/fedora/linux/releases/42/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-42-1.1.x86_64.qcow2","fedora",
     "#cloud-config\npassword: %s\nchpasswd: {expire: false}\nssh_pwauth: true\npackages: [git,curl,tmux,openssh-server]\nruncmd: [systemctl enable --now sshd]\n"},
};
#define NVMOS (sizeof(VMOS)/sizeof(*VMOS))
static const vmos_t*vm_find(const char*n){for(int i=0;i<(int)NVMOS;i++)if(!strcmp(VMOS[i].name,n))return&VMOS[i];return&VMOS[0];}

static int cmd_vm(int argc, char **argv) {
    if(argc>2&&!strcmp(argv[2],"android")){perf_disarm();return vm_android();}
    const char*sub=argc>2?argv[2]:"run";
    /* find OS arg */
    char*osn="debian";
    for(int i=2;i<argc;i++)for(int j=0;j<(int)NVMOS;j++)if(!strcmp(argv[i],VMOS[j].name)){osn=argv[i];break;}
    const vmos_t*os=vm_find(osn);
    char d[P],img[P],seed[P],log[P],ud[P],sd[P],md[P];
    snprintf(d,P,"%s/vm",AROOT);snprintf(img,P,"%s/%s.qcow2",d,os->name);
    snprintf(seed,P,"%s/%s-seed.iso",d,os->name);snprintf(log,P,"%s/console.log",d);
    char*port="2222",*pw="testvm1";
    char usr[128];snprintf(usr,128,"%s@localhost",os->user);
    perf_disarm();
    if(!strcmp(sub,"kill")){char c[B];snprintf(c,B,"pkill -f hostfwd=tcp::%s",port);return system(c);}
    if(!strcmp(sub,"snap")){
        char c[B],sb[P];snprintf(sb,P,"sshpass -p %s ssh -oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -oConnectTimeout=5 -p %s %s",pw,port,usr);
        snprintf(c,B,"%s 'sudo cp /boot/vmlinuz-* /tmp/k&&sudo cp /boot/initrd.img-* /tmp/i&&sudo chmod a+r /tmp/k /tmp/i' 2>/dev/null",sb);
        if(!system(c)){
            snprintf(c,B,"sshpass -p %s scp -oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -P %s %s:/tmp/k %s/vmlinuz&&sshpass -p %s scp -oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -P %s %s:/tmp/i %s/initrd",pw,port,usr,d,pw,port,usr,d);system(c);}
        snprintf(c,B,"echo savevm ready|socat -t2 - unix:%s/mon.sock 2>&1|tee /tmp/avmsn|grep -q Error",d);
        if(!system(c)){puts("x savevm failed:");(void)!system("tail -3 /tmp/avmsn");return 1;}
        puts("+ snapshot saved");return 0;
    }
    if(!strcmp(sub,"fast")){
        char vk[P],vi[P];snprintf(vk,P,"%s/vmlinuz",d);snprintf(vi,P,"%s/initrd",d);
        if(!fexists(vk)||!fexists(vi)){puts("x run 'a vm' then 'a vm snap' first (extracts kernel)");return 1;}
        pid_t p=fork();
        if(p==0){setsid();close(0);int fd=open(log,O_WRONLY|O_CREAT|O_TRUNC,0644);dup2(fd,1);dup2(fd,2);
            char di[P];snprintf(di,P,"file=%s,id=hd0,format=qcow2,if=none",img);
            execlp("qemu-system-x86_64","qemu-system-x86_64","-machine","microvm,rtc=on,pit=on","-enable-kvm","-cpu","host","-m","1G","-smp","2","-kernel",vk,"-initrd",vi,"-append","root=/dev/vda1 rw console=ttyS0 reboot=t panic=-1 quiet","-serial","file:/tmp/avm.log","-display","none","-device","virtio-blk-device,drive=hd0","-drive",di,"-device","virtio-net-device,netdev=n0","-netdev","user,id=n0,hostfwd=tcp::2222-:22",(char*)0);_exit(1);}
        struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
        printf("microvm booting (pid %d)...\n",(int)p);
        for(int i=0;i<10;i++){sleep(1);
            char c[B];snprintf(c,B,"sshpass -p %s ssh -oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null -oConnectTimeout=2 -p %s %s 'echo ready' 2>/dev/null",pw,port,usr);
            if(!system(c)){clock_gettime(CLOCK_MONOTONIC,&t1);
                double s=(double)(t1.tv_sec-t0.tv_sec)+(double)(t1.tv_nsec-t0.tv_nsec)/1e9;
                printf("+ %.1fs\n",s);char*sv[]={argv[0],"vm","ssh",osn,NULL};return cmd_vm(4,sv);}}
        puts("x microvm timeout — check /tmp/avm.log");return 1;
    }
    if(!strcmp(sub,"test")){
        char cmd[B];
        snprintf(cmd,B,"sshpass -p '%s' ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -p %s %s 'echo ok' 2>/dev/null",pw,port,usr);
        if(system(cmd)){char*rv[]={"a","vm","run",osn,NULL};if(cmd_vm(4,rv))return 1;}
        printf("> replicating a...\n");
        snprintf(cmd,B,"a new -p %s %s:%s",pw,usr,port);system(cmd);
        printf("> installing claude (native) + applying oauth creds from host...\n");
#ifdef __APPLE__
        const char*getcreds="security find-generic-password -s 'Claude Code-credentials' -w 2>/dev/null";
#else
        const char*getcreds="cat ~/.claude/.credentials.json 2>/dev/null";
#endif
        snprintf(cmd,B,"%s | sshpass -p '%s' ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p %s %s 'mkdir -p ~/.claude&&cat>~/.claude/.credentials.json&&chmod 600 ~/.claude/.credentials.json;command -v claude>/dev/null||curl -fsSL https://claude.ai/install.sh|bash' 2>&1",getcreds,pw,port,usr);
        system(cmd);
        printf("> running claude on codebase...\n");
        snprintf(cmd,B,"sshpass -p '%s' ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p %s %s 'export PATH=$HOME/.local/bin:$PATH && cd ~/a && claude -p --dangerously-skip-permissions \"Read the codebase with a cat 3. Describe what this project is and list all commands.\"' 2>&1",pw,port,usr);
        system(cmd);
        printf("\n> tearing down VM...\n");
        snprintf(cmd,B,"pkill -f hostfwd=tcp::%s",port);system(cmd);
        puts("+ done");return 0;
    }
    if(!strcmp(sub,"ssh")){
        char*a[]={"sshpass","-p",pw,"ssh","-o","StrictHostKeyChecking=no","-o","UserKnownHostsFile=/dev/null","-o","ConnectTimeout=10","-o","ServerAliveInterval=10","-p",port,usr,NULL};
        char*a2[20];int n=0;for(int i=0;a[i];i++)a2[n++]=a[i];
        for(int i=3;i<argc&&n<19;i++)if(strcmp(argv[i],osn))a2[n++]=argv[i];
        a2[n]=NULL;execvp(a2[0],a2);return 1;}
    mkdirp(d);
    char c[B];
    if(!fexists(img)){
        printf("> downloading %s cloud image...\n",os->name);
        if(system("command -v qemu-system-x86_64 >/dev/null")){puts("x install qemu: sudo pacman -S qemu-full cdrtools");return 1;}
        snprintf(c,B,"curl -fsSLo '%s' '%s'&&qemu-img resize '%s' 20G",img,os->url,img);
        if(system(c)){puts("x download failed");return 1;}
    }
    snprintf(ud,P,"%s/%s-user-data",d,os->name);
    {FILE*f=fopen(ud,"w");if(!f)return 1;fprintf(f,os->cloudinit,pw);fclose(f);}
    snprintf(sd,P,"%s/%s-seed",d,os->name);mkdirp(sd);
    snprintf(c,B,"cp '%s' '%s/user-data'",ud,sd);system(c);
    snprintf(md,P,"%s/meta-data",sd);writef(md,"{\"instance-id\":\"vm0\"}");
    snprintf(c,B,"xorriso -as mkisofs -o '%s' -V cidata -J -r '%s/' 2>&1",seed,sd);
    if(system(c)){puts("x xorriso failed");return 1;}
    int hassnap=0;{char cc[B];snprintf(cc,B,"qemu-img snapshot -l '%s' 2>/dev/null|grep -qw ready",img);hassnap=!system(cc);}
    pid_t p=fork();
    if(p==0){setsid();close(0);int fd=open(log,O_WRONLY|O_CREAT|O_TRUNC,0644);dup2(fd,1);dup2(fd,2);
        char di[P],mon[P];snprintf(di,P,"file=%s,format=qcow2,if=virtio",img);snprintf(mon,P,"unix:%s/mon.sock,server,nowait",d);
        char*av[32];int n=0;
        av[n++]="qemu-system-x86_64";
#ifdef __APPLE__
        av[n++]="-cpu";av[n++]="max";
#else
        av[n++]="-cpu";av[n++]="host";av[n++]="-enable-kvm";
#endif
        av[n++]="-m";av[n++]="4G";av[n++]="-smp";av[n++]="2";
        av[n++]="-drive";av[n++]=di;av[n++]="-cdrom";av[n++]=seed;
        av[n++]="-device";av[n++]="virtio-net-pci,netdev=n0";av[n++]="-netdev";av[n++]="user,id=n0,hostfwd=tcp::2222-:22";
        av[n++]="-nographic";av[n++]="-monitor";av[n++]=mon;
        if(hassnap){av[n++]="-loadvm";av[n++]="ready";}
        av[n]=NULL;execvp(av[0],av);_exit(1);}
    struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
    printf("booting %s%s (pid %d)...\n",os->name,hassnap?" [snap]":"",(int)p);
#ifdef __APPLE__
    int tries=hassnap?30:60,wait=hassnap?2:10;
#else
    int tries=hassnap?10:12,wait=hassnap?1:10;
#endif
    for(int i=0;i<tries;i++){sleep((unsigned)wait);
        snprintf(c,B,"sshpass -p %s ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -p %s %s 'echo ready' 2>/dev/null",pw,port,usr);
        if(!system(c)){clock_gettime(CLOCK_MONOTONIC,&t1);
            double s=(double)(t1.tv_sec-t0.tv_sec)+(double)(t1.tv_nsec-t0.tv_nsec)/1e9;
            printf("+ %.1fs\n",s);char*sv[]={argv[0],"vm","ssh",osn,NULL};return cmd_vm(4,sv);}
        printf("  waiting (%ds)...\n",(i+1)*wait);}
    printf("x timeout — check %s\n",log);return 1;
}
