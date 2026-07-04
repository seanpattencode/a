# a aram — remote RAM: lend local RAM as nbd swap / borrow a donor's. Soft cut drains pages home (nothing dies);
# donor death SIGBUSes only processes with remote-only pages, kernel+rest fine. ARAM_BIND=127.0.0.1 for tunnel-only lend.
D=/dev/shm/aram
case "$1" in
lend) truncate -s "${2:-1024}M" $D.img&&printf '[generic]\nlistenaddr = %s\nport = 10809\n[aram]\nexportname = %s.img\n' "${ARAM_BIND:-0.0.0.0}" "$D" >$D.conf&&nbd-server -C $D.conf&&echo "✓ lending ${2:-1024}M on :10809";;
unlend) killall nbd-server 2>/dev/null;rm -f $D.img $D.conf;echo "✓ unlent";;
borrow) [ -n "$2" ]||{ echo "x need donor host";exit 1;};sudo modprobe nbd&&sudo nbd-client -N aram "$2" "${3:-10809}" /dev/nbd0 -t 8&&sudo mkswap /dev/nbd0 >/dev/null&&sudo swapon -p 200 /dev/nbd0&&echo "✓ borrowing — detach: a aram cut";;
cut) sudo swapoff /dev/nbd0&&sudo nbd-client -d /dev/nbd0&&echo "✓ cut clean — pages drained home";;
status) cat /proc/swaps;pgrep -x nbd-server>/dev/null&&echo "lending: $(du -sh $D.img 2>/dev/null|cut -f1) served";;
*) echo "a aram lend [MB] | unlend | borrow <host> [port] | cut | status";;
esac
