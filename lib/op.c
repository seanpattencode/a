/* op — claude operator in tmux. plain english → a cmds. reattach per dir. */
static const char *OPERATOR_PROMPT =
"Operator for `a`. User talks plain English; you translate to `a` commands and run them via Bash.\n\n"
"Rules:\n"
"- Read `a help` once to know commands. Source: lib/ files in this repo (cat a.c index).\n"
"- Run command, show user the actual output. Don't summarize, don't paraphrase — output is truth, your prose isn't.\n"
"- Confirm before: push, pull (nukes local), rm, kill, ssh that runs commands on other devices.\n"
"- Never push without explicit user \"push\" / \"yes push\".\n"
"- If user request maps to no `a` cmd, say so in one line — don't invent.\n"
"- One short sentence per turn unless user asks for detail. They want results, not narration.\n\n"
"Examples:\n"
"\"send notes.txt to laptop\"  → `a ssh laptop 'cat > /tmp/notes.txt' < notes.txt`\n"
"\"what's running\"             → `a ls` then `a job`\n"
"\"jump to alpha\"              → `a alpha` (or numbered project)\n"
"\"new note: idea X\"           → `a n \"idea X\"`\n"
"\"sync\"                       → `a push` (after confirm) or `a pull`\n";

static int cmd_op(int c,char**v){(void)c;(void)v;perf_disarm();
    init_db();load_cfg();CWD(wd);
    char pp[P];snprintf(pp,P,"%s/common/prompts/operator.txt",SROOT);
    if(access(pp,R_OK)!=0){
        snprintf(pp,P,"%s/operator_prompt.txt",TMP);
        FILE*f=fopen(pp,"w");if(f){fputs(OPERATOR_PROMPT,f);fclose(f);}
    }
    char cmd[B];snprintf(cmd,B,"claude --model opus --dangerously-skip-permissions --effort max --append-system-prompt-file %s",pp);
    char sn[64];snprintf(sn,64,"op-%s",bname(wd));
    if(!tm_has(sn))create_sess(sn,wd,cmd,NULL);
    tm_go(sn);return 0;}
