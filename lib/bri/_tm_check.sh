# tm_check <host> — wait until an eval-capable bri client is live on $host before the
# recipe fires its eval steps. The eval steps (select Deep-Research mode, type, submit)
# only work through bri-ext (source:extension) — userscript eval is CSP-blocked on
# chatgpt/claude/gemini. bri-ext injects a few seconds AFTER the userscript on a fresh
# tab, so we wait for ITS hello, not just any client; firing too early = empty composer.
# Sourced by lib/bri/<provider>-dr.sh; the recipes call: tm_check claude.ai || exit 2
tm_check() {
    local host="$1" i
    for i in $(seq 1 12); do
        grep '"hello"' /tmp/bri.log 2>/dev/null | grep "://$host/" | grep -q '"source":"extension"' && return 0
        sleep 2
    done
    if grep '"hello"' /tmp/bri.log 2>/dev/null | grep "://$host/" | grep -qv '"source":"extension"'; then
        echo "! bri-ext not ready on $host — eval steps will fail (userscript eval is CSP-blocked)." >&2
        echo "  Load it: a bri deploy   then re-run." >&2; return 0
    fi
    echo "! no bri client active on $host (CSP/suspension blocker)." >&2
    echo "  Load bri-ext (a bri deploy) or install the userscript, reload $host, re-run." >&2
    firefox-nightly --new-tab "http://127.0.0.1:1234/a.user.js" 2>/dev/null & disown
    return 1
}
