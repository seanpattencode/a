/* a new [-p pw] <host|user@host[:port]> (alias: a clone) — replicate a onto a device. Registry name -> a ssh (its auth + fallbacks); raw target -> ssh (-p pw = sshpass). Remote: install from main (curl|sh), then this box's gh hosts.yml + git identity, then adata/git re-cloned sparse without the activity tree (a-git push pathology). new.c+clone.c merged 2026-09-05 (Sean). */
static int cmd_new(int argc,char**argv){
    perf_disarm();int ai=2;const char*pw=NULL;
    if(argc>3&&!strcmp(argv[2],"-p")){pw=argv[3];ai=4;}
    if(ai>=argc){puts("Usage: a new [-p pw] <host|user@host[:port]>");return 1;}
    char enc[B]="",nm[128]="",em[128]="",rem[B];
    pcmd("base64 -w0 ~/.config/gh/hosts.yml 2>/dev/null|tr -d '\\n'",enc,B);
    pcmd("git config --global user.name",nm,128);nm[strcspn(nm,"\n")]=0;
    pcmd("git config --global user.email",em,128);em[strcspn(em,"\n")]=0;
    int l=snprintf(rem,B,"export PATH=$HOME/.local/bin:$PATH;curl -fsSL https://raw.githubusercontent.com/seanpattencode/a/main/a.c|sh;");
    if(enc[0])l+=snprintf(rem+l,(size_t)(B-l),"mkdir -p ~/.config/gh;echo %s|base64 -d>~/.config/gh/hosts.yml;gh auth setup-git 2>/dev/null;cd ~/a&&rm -rf adata/git&&gh repo clone seanpattencode/a-git adata/git -- --depth=1 --no-checkout&&cd adata/git&&printf \"/*\\n!/activity\\n\"|git sparse-checkout set --no-cone --stdin&&git checkout;",enc);
    if(nm[0])l+=snprintf(rem+l,(size_t)(B-l),"git config --global user.name \"%s\";",nm);
    if(em[0])snprintf(rem+l,(size_t)(B-l),"git config --global user.email \"%s\";",em);
    if(!strchr(argv[ai],'@')){execlp("a","a","ssh",argv[ai],rem,(char*)0);return 127;}
    char hp[256],port[8];ssh_parse(argv[ai],hp,port);if(pw)setenv("SSHPASS",pw,1);
    char*av[]={"sshpass","-e","ssh","-oStrictHostKeyChecking=no","-oUserKnownHostsFile=/dev/null","-oConnectTimeout=10","-tt","-p",port,hp,rem,0};
    execvp(av[pw?0:2],av+(pw?0:2));return 127;}
