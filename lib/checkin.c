/* a checkin [session] [prompt] — wake an idle LLM tmux window with a prompt (default: terse status-email nudge).
   Converted from lib/checkin.sh per IDEAS #130/#131. Calls cmd_send directly (no `a send` re-exec); uses DEV (no ssh-file scan). */
static int cmd_checkin(int c, char **v) {
    perf_disarm();
    char sn[128] = ""; int ai = 2;
    if (c > 2) { snprintf(sn, 128, "%s", v[2]); ai = 3; }
    else { pcmd("tmux display-message -p '#{window_name}' 2>/dev/null", sn, 128); sn[strcspn(sn, "\n")] = 0; }
    if (!sn[0]) { puts("usage: a checkin [session] [prompt] (no tmux window detected)"); return 1; }
    char p[B] = ""; int pl = 0;
    for (int i = ai; i < c; i++) pl += snprintf(p + pl, (size_t)(B - pl), "%s%s", pl ? " " : "", v[i]);
    if (!p[0]) snprintf(p, B, "send a email on status");
    char fp[B]; snprintf(fp, B, "%s (append [dev=%s win=%s] to email subject)", p, DEV, sn);
    char *sv[] = { "a", "send", sn, fp, NULL };
    return cmd_send(4, sv);
}
