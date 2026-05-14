static int cmd_m(int c,char**v){(void)c;(void)v;
    char b[B],sf[P],ss[P];
    snprintf(b,B,"[ -d %1$s/m ]||(cd %1$s&&gh repo create m --private --clone)",AROOT);system(b);
    snprintf(sf,P,"%s/m/m.txt",AROOT);snprintf(ss,P,"%s/m_status",TMP);
    if(!fexists(sf)){FILE*f=fopen(sf,"w");if(f){fputs("## user\n",f);fclose(f);}}
    {FILE*f=fopen(ss,"w");if(f){fputs("ready",f);fclose(f);}}
    if(!getenv("TMUX")){puts("x needs tmux");return 1;}
    snprintf(b,B,"tmux split-window -dvb 'e %s'",sf);system(b);
    snprintf(b,B,"tmux split-window -dv -l 1 'P(){ printf \"\\r\\033[K%%s\" \"$(cat %1$s)\";};P;while inotifywait -qe modify %1$s 2>/dev/null;do P;done'",ss);system(b);
    snprintf(b,B,"while :;do T=$(mktemp);e --box message: \"$T\";[ -s \"$T\" ]&&{ echo sending>%2$s;{ cat \"$T\";printf \"\\n## assistant\\n\";cat %1$s|claude -p;printf \"\\n## user\\n\";} >>%1$s;echo done>%2$s;};rm \"$T\";done",sf,ss);
    execlp("sh","sh","-c",b,(char*)0);return 1;
}
