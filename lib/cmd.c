/* a cmd — shell passthrough, 25us; system() not execvp for pipes/globs */
static int cmd_cmd(int c,char**v){if(c<3)return 1;
char cm[B]="";for(int i=2;i<c;i++)snprintf(cm+strlen(cm),(size_t)(B-strlen(cm)),"%s%s",i>2?" ":"",v[i]);
return system(cm);}
