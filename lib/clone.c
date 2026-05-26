static int cmd_clone(int argc, char **argv) {
    char enc[B];
    if (argc < 3 || pcmd("base64 -w0 ~/.config/gh/hosts.yml|tr -d '\\n'", enc, B) || !enc[0]) { puts("a clone <ssh-host>"); return 1; }
    char rem[B];
    snprintf(rem, B, "mkdir -p ~/.config/gh;echo %s|base64 -d>~/.config/gh/hosts.yml;curl -fsSL https://raw.githubusercontent.com/seanpattencode/a/main/a.c|sh;gh auth setup-git;cd ~/a;rm -rf adata/git;gh repo clone seanpattencode/a-git adata/git -- --depth=1 --no-checkout;cd adata/git;printf '/*\\n!/activity\\n'|git sparse-checkout set --no-cone --stdin;git checkout", enc);
    execlp("a", "a", "ssh", argv[2], rem, (char*)NULL);
    return 127;
}
