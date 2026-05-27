# tm_check <host> — verify TM userscript is active on $host, else auto-open
# the userscript install URL in Firefox and tell the user what to click.
# Sourced by lib/bri/<provider>-dr.sh; the recipes call: tm_check claude.ai || exit 2
#
# Detection: TM userscript's hello has no "source" field (bri-ext sets
# "source":"extension"). If we see ANY hello matching the host without
# "source", TM is active. Five attempts × 2s = 10s window for hello to fire.
tm_check() {
    local host="$1" i
    for i in 1 2 3 4 5; do
        if grep '"hello"' /tmp/bri.log 2>/dev/null | grep "://$host/" | grep -v '"source"' | head -1 >/dev/null; then
            return 0
        fi
        sleep 2
    done
    echo "! TM userscript not active on $host (CSP/suspension blocker)." >&2
    echo "  Opening install URL — click 'Install' in the Tampermonkey popup," >&2
    echo "  then reload $host and re-run this recipe." >&2
    firefox-nightly --new-tab "http://127.0.0.1:1234/a.user.js" 2>/dev/null & disown
    return 1
}
