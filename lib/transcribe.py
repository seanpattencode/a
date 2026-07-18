"""a transcribe <file|folder|log|watch|on|off|status> — faster-whisper + VAD; on/off/status/watch = trwatch.service, gdrive EVR -> parakeet -> transcripts. Manual file specification is brittle: filename date != upload date, mod times drift, glob patterns over rclone listings produce surprising matches. Pass exact paths only."""
import sys, os, subprocess as sp, time, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import ADATA_ROOT

OUT = str(ADATA_ROOT / 'transcripts')
TMP = str(ADATA_ROOT / 'local' / 'tmp')
LOG = str(ADATA_ROOT / 'git' / 'transcribe_log.jsonl')
EXTS = ('.flac', '.wav', '.m4a', '.mp3', '.ogg', '.opus', '.3gp', '.amr')
MODEL, BACKEND = 'small', 'faster-whisper int8 cpu_threads=20 vad_filter=True'
RECENT_DIR = 'a-gdrive2:Archive/Files/Easy Voice Recorder'

def ensure_deps():
    try: import faster_whisper, httpx, fsspec
    except: os.system('pip install --user --break-system-packages faster-whisper httpx fsspec')

_M = _P = None
PK = 'sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8'
def parakeet_stream(path):
    global _P, MODEL, BACKEND
    import numpy as np
    c = os.path.expanduser('~/.cache/a_dictate/'); d = c + PK
    if _P is None:
        import sherpa_onnx
        if not os.path.isdir(d): sp.run(f'mkdir -p {c} && cd {c} && wget -q https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/{PK}.tar.bz2 && tar xf *.tar.bz2 && rm -f *.tar.bz2', shell=True, check=True)
        _P = sherpa_onnx.OfflineRecognizer.from_transducer(encoder=d+'/encoder.int8.onnx', decoder=d+'/decoder.int8.onnx', joiner=d+'/joiner.int8.onnx', tokens=d+'/tokens.txt', num_threads=6, model_type='nemo_transducer')
    MODEL, BACKEND = 'parakeet-tdt-0.6b-v2', 'sherpa-onnx int8 cpu 30s-chunk rms-gate'
    t = time.time(); parts = []; sec = 0
    p = sp.Popen(['ffmpeg', '-v', 'quiet', '-i', path, '-f', 's16le', '-ac', '1', '-ar', '16000', '-'], stdout=sp.PIPE)
    while b := p.stdout.read(960000):
        a = np.frombuffer(b, np.int16).astype(np.float32) / 32768.0
        if float((a * a).mean()) ** .5 > .003:
            s = _P.create_stream(); s.accept_waveform(16000, a); _P.decode_stream(s)
            if txt := s.result.text.strip():
                print(f'  [{sec/3600:6.2f}h] {txt}', flush=True); parts.append(txt)
        sec += 30
    p.wait()
    return ' '.join(parts).strip(), time.time() - t

def transcribe_stream(path, model='small'):
    global _M
    if os.environ.get('A_TR_PK'): return parakeet_stream(path)
    if _M is None:
        from faster_whisper import WhisperModel
        _M = WhisperModel(model, device='cpu', compute_type='int8', cpu_threads=20)
    t = time.time()
    segs, _ = _M.transcribe(path, language='en', condition_on_previous_text=False, vad_filter=True)
    parts = []
    for s in segs:
        txt = s.text.strip()
        if txt:
            print(f'  [{s.start:6.1f}-{s.end:6.1f}] {txt}', flush=True)
            parts.append(txt)
    return ' '.join(parts).strip(), time.time() - t

def is_remote(p): return ':' in p and not p.startswith('/')

def _j(l):
    try: return json.loads(l)
    except ValueError: return None   # survive git conflict markers / partial lines in the shared jsonl

def log_done():
    if not os.path.exists(LOG): return set()
    return {r['src'] for l in open(LOG) if (r := _j(l))}

def log_write(rec):
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    open(LOG, 'a').write(json.dumps(rec) + '\n')

def list_remote(p):
    r = sp.run(['rclone', 'lsf', p], capture_output=True, text=True)
    return [f.strip() for f in r.stdout.strip().split('\n') if f.strip().lower().endswith(EXTS)]

def process(target, done=None, ldir=TMP):
    if done and target in done: print(f'skip (logged): {target}'); return
    lp = None; remote_dir = None
    try:
        if is_remote(target):
            remote_dir = target.rsplit('/', 1)[0] if '/' in target else target
            lp = os.path.join(ldir, os.path.basename(target))
            print(f'Downloading {target}...', flush=True)
            sp.run(['rclone', 'copy', target, ldir], check=True)
        else: lp = target
        sz = os.path.getsize(lp)
        print(f'Transcribing {os.path.basename(lp)} ({sz/1e6:.1f}MB)...', flush=True)
        txt, dur = transcribe_stream(lp)
        rec = {'ts': time.strftime('%Y-%m-%dT%H:%M:%S'), 'src': target, 'bytes': sz, 'proc_s': round(dur,1), 'chars': len(txt), 'model': MODEL, 'backend': BACKEND, 'preview': txt[:200]}
        if txt:
            base = os.path.splitext(os.path.basename(lp))[0]
            out = os.path.join(OUT, base + '.txt')
            open(out, 'w').write(txt + '\n')
            print(f'{dur:.0f}s | {len(txt)} chars | {out}')
            if remote_dir:
                sp.run(['rclone', 'copy', out, remote_dir], check=True)
                print(f'uploaded -> {remote_dir}/{base}.txt')
        else: print('(no speech detected)')
        log_write(rec)
    finally:
        if remote_dir and lp and lp.startswith(ldir) and os.path.exists(lp):
            os.remove(lp); print(f'deleted local: {lp}\n', flush=True)

def run():
    os.makedirs(OUT, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    ensure_deps()
    args = sys.argv[2:]
    if not args:
        print('Usage: a transcribe <file|folder|recent [N] [folder]|new [folder]|log|watch|on|off|status>\n  file:   local path or remote:path/file.flac\n  folder: local dir or remote:path\n  recent [N] [folder]: N most recently modified audio files (default N=10, folder = Easy Voice Recorder)\n  new [folder]: every unlogged audio file in folder, newest first (default = Easy Voice Recorder)\n  log:    show transcription history\n  on|off|status: parakeet auto-transcribe service (status = live stream)  watch: run loop in foreground')
        return
    if args[0] == 'watch':
        import fcntl; lk = open('/tmp/a_trwatch.lock', 'w')
        try: fcntl.flock(lk, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError: print('x watch already running'); return
        os.environ['A_TR_PK'] = '1'
        L = os.path.expanduser('~/evr_local'); os.makedirs(L, exist_ok=True)
        print(f'+ trwatch {time.strftime("%FT%T")} parakeet · poll 60s · {RECENT_DIR} -> {OUT}', flush=True)
        while True:
            done = log_done()
            r = sp.run(['rclone', 'lsf', RECENT_DIR], capture_output=True, text=True)
            for n in sorted(map(str.strip, r.stdout.split('\n')), reverse=True):
                if n.lower().endswith(EXTS) and f'{RECENT_DIR}/{n}' not in done:
                    try: process(f'{RECENT_DIR}/{n}', ldir=L)
                    except Exception as e: print(f'  ! {n}: {e}', flush=True)
            time.sleep(60)
    if args[0] in ('on', 'off', 'status'):
        sc = lambda *a, **k: sp.run(['systemctl', '--user', *a, 'trwatch.service'], **k)
        bg = str(ADATA_ROOT / 'local' / 'bg' / 'trwatch-systemd.log'); H = os.path.expanduser('~')
        if args[0] == 'on':
            u = H + '/.config/systemd/user/trwatch.service'
            for x in (u, bg): os.makedirs(os.path.dirname(x), exist_ok=True)
            open(u, 'w').write(f'[Unit]\nDescription=trwatch - gdrive EVR -> parakeet -> transcripts\nAfter=network-online.target\n[Service]\nEnvironment=HOME={H}\nEnvironment=PATH={H}/.local/bin:/usr/local/bin:/usr/bin:/bin\nExecStart={H}/.local/bin/a transcribe watch\nRestart=always\nRestartSec=30\nNice=10\nStandardOutput=append:{bg}\nStandardError=append:{bg}\n[Install]\nWantedBy=default.target\n')
            sp.run(['systemctl', '--user', 'daemon-reload']); sc('enable', capture_output=True); sc('restart')
            if b'Linger=yes' not in sp.run(['loginctl', 'show-user', os.environ.get('USER', ''), '-p', 'Linger'], capture_output=True).stdout: print('! linger off (won\'t survive logout): sudo loginctl enable-linger')
        if args[0] == 'off': sc('disable', '--now')
        st = sc('is-active', capture_output=True, text=True).stdout.strip()
        print(f'trwatch: {st} · engine parakeet · log {bg}\n  a transcribe on|off · a transcribe status = live stream')
        if args[0] == 'status' and os.path.exists(bg):
            for l in open(bg).readlines()[-10:]: print(' ', l.rstrip())
            if st == 'active' and sys.stdout.isatty(): print('-- live (^C exits) --'); sys.stdout.flush(); os.execvp('tail', ['tail', '-F', bg])
        return
    if args[0] in ('recent', 'new'):
        is_new = args[0] == 'new'
        if is_new:
            n = None
            folder = ' '.join(args[1:]) if len(args) > 1 else RECENT_DIR
        else:
            n = int(args[1]) if len(args) > 1 and args[1].isdigit() else 10
            folder = ' '.join(args[2:]) if len(args) > 2 else RECENT_DIR
        r = sp.run(['rclone', 'lsl', folder], capture_output=True, text=True)
        files = []
        for l in r.stdout.strip().split('\n'):
            parts = l.strip().split(None, 3)
            if len(parts) < 4: continue
            name = parts[3]
            if name.lower().endswith(EXTS): files.append((parts[1] + 'T' + parts[2], name))
        files.sort(reverse=True)
        done = log_done()
        if is_new:
            files = [(ts, n_) for ts, n_ in files if f'{folder}/{n_}' not in done]
            print(f'{len(files)} unlogged files in {folder} (newest first)')
        else:
            files = files[:n]
            print(f'{len(files)} most recent in {folder}:')
            for ts, n_ in files: print(f'  {ts} {n_}')
        print()
        for _, name in files: process(f'{folder}/{name}', done)
        return
    if args[0] == 'log':
        if not os.path.exists(LOG): print('(no log)'); return
        recs = [r for l in open(LOG) if (r := _j(l))]
        ok = sum(1 for r in recs if r['chars'] > 0); empty = len(recs) - ok
        tot_b = sum(r['bytes'] for r in recs); tot_s = sum(r['proc_s'] for r in recs)
        print(f'{len(recs)} runs | {ok} ok | {empty} empty | {tot_b/1e9:.1f}GB | {tot_s/60:.1f}min proc')
        backends = {r['backend'] for r in recs}
        for b in backends: print(f'  backend: {b}')
        print()
        for r in recs[-20:]:
            mark = '✓' if r['chars'] else '∅'
            print(f"  {mark} {r['ts']} {r['proc_s']:>6.1f}s {r['chars']:>6}ch  {r['src'].rsplit('/',1)[-1][:50]}")
            if r['chars']: print(f"      {r['preview'][:120]}")
        return
    target = ' '.join(args)
    done = log_done()
    if is_remote(target):
        if target.lower().endswith(EXTS): process(target, done)
        else:
            files = list_remote(target)
            print(f'{len(files)} audio files in {target}')
            for f in files: process(f'{target}/{f}', done)
    elif os.path.isdir(target):
        files = sorted(f for f in os.listdir(target) if f.lower().endswith(EXTS))
        print(f'{len(files)} audio files in {target}')
        for f in files: process(os.path.join(target, f), done)
    else: process(target, done)

try: run()
except KeyboardInterrupt: print('\n^C — stopped. Re-run to resume from log.', file=sys.stderr); sys.exit(130)
