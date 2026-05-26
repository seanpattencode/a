static int cmd_clone(int argc, char **argv) {
    if (argc < 3) { puts("a clone <ssh-host>"); return 1; }
    char enc[B];
    if (pcmd("base64 -w0 ~/.config/gh/hosts.yml 2>/dev/null", enc, B) || !enc[0]) { puts("a clone <ssh-host>"); return 1; }
    enc[strcspn(enc,"\n")] = 0;
    char rem[B*2];
    snprintf(rem, B*2, "mkdir -p ~/.config/gh;echo %s|base64 -d>~/.config/gh/hosts.yml;chmod 600 ~/.config/gh/hosts.yml;curl -fsSL https://raw.githubusercontent.com/seanpattencode/a/main/a.c|sh;gh auth setup-git;cd ~/a;rm -rf adata/git;gh repo clone seanpattencode/a-git adata/git", enc);
    execlp("a", "a", "ssh", argv[2], rem, (char*)NULL);
    return 127;
}
