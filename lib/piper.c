/* a piper — DISABLED (default voice en_GB-alan-medium sounds like an evil AI). Re-enable when a voice is picked.
   Working snapshot: adata/git/my/piper_alan.sh; voice exploration: adata/git/my/piper_voices.sh. Converted from lib/piper.sh per IDEAS #130/#131. */
static int cmd_piper(int c, char **v) { (void)c; (void)v;
    puts("a piper: disabled while picking a non-villain voice. see adata/git/my/piper_voices.sh");
    return 0;
}
