#if 0 // /// script — python half of `a book`; C TUI half after #endif (one-file merge, Sean 2026-07-18)
# /// script
# dependencies = ["PyPDF2"]
# ///
import sys, subprocess, time, tempfile, os, shutil, glob
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

sys.argv = sys.argv[1:]  # shift: a prepends "book" to argv
ROOT = Path(__file__).resolve().parent.parent
ADATA = ROOT / "adata"
DATA_DIR = ADATA / "books"

def _idxw(IDX, rows):   # atomic replace (unique tmp+rename) — bare write_text raced cross-device writers: spliced rows, catalog truncated to 1 line (7/15, repaired a-git eba10f1e9f)
    t = IDX.parent / f".idx{os.getpid()}"; t.write_text("\n".join(rows) + "\n"); t.rename(IDX)

POSD = ADATA / "local" / "bookpos"
def _pos_w(name, off):   # position register: local mirror instantly + gdrive fire-and-forget → other devices see it in seconds, not at next git merge
    POSD.mkdir(parents=True, exist_ok=True); (POSD / name).write_text(str(off))
    subprocess.Popen(["rclone", "copyto", str(POSD / name), f"a-gdrive:books/pos/{name}", "--contimeout", "5s", "--retries", "2"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def _pos_r(name, seed=0):   # cloud-first, HARD 4s deadline (subprocess timeout= — rclone --timeout is idle-IO, NOT a deadline: hung the reader open under a sync storm), loud fallback → mirror → col5 seed
    POSD.mkdir(parents=True, exist_ok=True); t = POSD / (name + ".new")
    print(">> position: cloud…", end="", flush=True)
    try:
        r = subprocess.run(["rclone", "copyto", f"a-gdrive:books/pos/{name}", str(t), "--contimeout", "2s", "--retries", "1", "--low-level-retries", "2"], capture_output=True, timeout=4)
        if t.exists(): t.rename(POSD / name); print(" ✓")
        else: print(f" fail rc{r.returncode} → local" if r.returncode else " no register → local")
    except subprocess.TimeoutExpired: print(" timeout 4s → local")
    except Exception as e: print(f" {type(e).__name__} → local")
    try: return int((POSD / name).read_text())
    except Exception: return seed

def pick_book():
    books = sorted(p.parent.name for p in DATA_DIR.glob("[!.]*/source.*"))
    if not books: print("No books in", DATA_DIR); sys.exit(1)
    for i, b in enumerate(books, 1): print(f"{i}. {b}")
    return DATA_DIR / books[int(input("Select: ")) - 1]

def resolve_book(name=None):
    if not name: return pick_book()
    p = Path(name)
    if p.is_dir(): return p
    if (DATA_DIR / name).is_dir(): return DATA_DIR / name
    print(f"Book not found: {name}"); sys.exit(1)

def _codex(pdf, prompt, txt_path):
    tmp = f"/tmp/_ocr_{os.getpid()}_{Path(pdf).stem}"
    subprocess.run(["pdftoppm","-png","-r","200",str(pdf),tmp],capture_output=True)  # 200dpi: cleaner math/subscripts than 100
    pngs = sorted(glob.glob(f"{tmp}*.png"))
    if not pngs: return ""
    of = f"{tmp}.out"
    args = ["codex","exec",prompt,"--skip-git-repo-check","-o",of]
    for p in pngs: args += ["--image",p]
    subprocess.run(args,capture_output=True,timeout=300,stdin=subprocess.DEVNULL)  # codex blocks reading stdin without this
    t = Path(of).read_text().strip() if Path(of).exists() else ""
    for p in pngs+[of]:
        try: os.unlink(p)
        except: pass
    if not t: return ""
    out = t if "<transcription>" in t else f"<transcription>\n{t}\n</transcription>"
    txt_path.write_text(out); return out

def process_page(source_path, output_dir, prompt, nocache=False):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    txt_path = output_dir / f"{Path(source_path).stem}.txt"
    if not nocache and txt_path.exists() and txt_path.stat().st_size > 0: return txt_path.read_text()
    if os.environ.get("A_BOOK_CODEX") and str(source_path).endswith(".pdf"):   # force codex (vision→LaTeX); skip claude (it content-filters copyrighted scans)
        try:
            out = _codex(source_path, prompt, txt_path)                        # isolate per-page failures (e.g. codex 300s timeout) so one bad page can't crash the whole run
        except Exception as e:
            print(f"  ✗ codex {Path(source_path).name}: {type(e).__name__}", flush=True); return ""
        if out: print(f"  ✓ codex: {Path(source_path).name}", flush=True)
        return out
    for attempt in range(3):
        try:
            r = subprocess.run(["claude", "--dangerously-skip-permissions", "--print"], input=f"{prompt}: {source_path}", text=True, capture_output=True, timeout=120)
            if r.returncode == 0 and r.stdout.strip() and "API Error" not in r.stdout:
                txt_path.write_text(r.stdout); return r.stdout
            if "content filtering" in r.stdout: print(f"  filter→codex: {Path(source_path).name}"); break
            raise Exception(r.stderr or r.stdout[:200] or f"Exit {r.returncode}")
        except Exception as e:
            if attempt < 2: time.sleep(2 ** attempt)
            else: print(f"  claude failed {Path(source_path).name}: {e}")
    if str(source_path).endswith(".pdf"):
        out = _codex(source_path, prompt, txt_path)
        if out: print(f"  ✓ codex: {Path(source_path).name}"); return out
    return ""

_TPROMPT = "Read this file and transcribe it. Remove headers, footers, page numbers, and section labels like 'INTRODUCTION xix'. For any graphs/charts/images, include a description in the format 'Graph: [description]'. If the page is blank or has no meaningful content, return empty <transcription></transcription> tags. Return ONLY the main body text wrapped in <transcription></transcription> tags"
def transcribe_page(source_path, output_dir, nocache=False):
    return process_page(source_path, output_dir, _TPROMPT, nocache)

def translate_page(source_path, output_dir, target_lang="English", nocache=False):
    return process_page(source_path, output_dir, f"Translate this scanned page to {target_lang}, preserving paragraph structure and section headers (\\section*{{}}). Render ALL mathematics as LaTeX — $..$ inline, \\[..\\] display — transcribing every formula exactly; translate only the prose, never alter or omit a formula. Keep footnote markers. Blank page → empty <transcription></transcription> tags. Return only the translated text wrapped in <transcription></transcription>", nocache)

def latex_page(source_path, output_dir, nocache=False):
    return process_page(source_path, output_dir, "Transcribe to LaTeX body (no preamble/documentclass/begin{document}). $..$ inline, \\[..\\] display, \\section*{}, \\footnote{}, \\textit{}. Blank→empty tags. Wrap in <transcription></transcription>", nocache)

def explain_page(source_path, output_dir, technical=False, nocache=False):
    p = "Reproduce verbatim. Audience: college student with high-school math/physics only, no specialist background. Length is not a constraint — be thorough; assume nothing beyond high-school algebra and intro physics. Insert [explanations] after EVERY term, symbol, equation, or phrase a non-specialist might not fully grasp (e.g. 'wavefunction', 'vector space', 'inner product', 'singular', 'eigenvalue', 'polynomial growth', 'observable' all need inline explanation). After each equation: [spoken aloud — math operations in plain terms — plain meaning — WHY it appears, what it accomplishes, what its value tells us conceptually]. After each technical term: [plain explanation + why it matters here]. Example: 'AB - BA = 0 [A times B minus B times A equals zero — this difference is the commutator, measuring whether order of multiplication matters — zero means A and B give the same answer either way, they commute — significant because commuting operators correspond to measurements that can be made simultaneously without disturbing each other]'. Within one page, don't re-explain the exact same term twice. Never rewrite. Return only annotated text" if technical else "Reproduce this text verbatim, inserting [bracketed explanations] immediately after obscure terms that require context. Example: 'the Semyonovsky Regiment [elite Russian guard unit] was known for...' Rules: 1) Never rewrite - only insert [brackets] after words needing explanation 2) Skip well-known figures and common terms 3) Only explain what a reader cannot infer from context 4) Keep explanations to a few words 5) Less is more - when in doubt, skip it. Return annotated text only"
    return process_page(source_path, output_dir, p, nocache)

def process_range(book_dir, start, end, page_func, source_subdir, cache_subdir, suffix, workers=1, total_pages=None, **kwargs):
    book_dir = Path(book_dir)
    source_dir, cache_dir, output_dir = book_dir / source_subdir, book_dir / cache_subdir, book_dir / "output"
    output_dir.mkdir(exist_ok=True)
    ext = ".txt" if source_subdir in ("translations", "transcriptions") else ".pdf"
    pages = [str(source_dir / f"page_{i:04d}{ext}") for i in range(start, end + 1)]
    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(lambda p: page_func(p, str(cache_dir), **kwargs), pages))
    cleaned = [r.replace("<transcription>", "").replace("</transcription>", "").strip() for r in results]
    fname = f"{suffix}.txt" if total_pages and start == 1 and end == total_pages else f"{suffix}-pages_{start:04d}-{end:04d}.txt"
    (output_dir / fname).write_text("\n\n".join(cleaned))
    print(f"Processed pages {start}-{end}, saved to {output_dir / fname}")

def split_pdf(book_dir, nocache=False):
    from PyPDF2 import PdfReader, PdfWriter
    book_dir = Path(book_dir)
    pages_dir = book_dir / "pages"
    if not nocache and pages_dir.exists() and list(pages_dir.glob("*.pdf")):
        print(f"Using cached pages from {pages_dir}"); return
    pages_dir.mkdir(parents=True, exist_ok=True)
    reader = PdfReader(str(book_dir / "source.pdf"))
    def save(args):
        r, i, d = args
        try:
            w = PdfWriter(); w.add_page(r.pages[i])
            with open(d / f"page_{i+1:04d}.pdf", "wb") as f: w.write(f)
        except Exception as e: print(f"Skipping page {i+1}: {type(e).__name__}")
    with ThreadPoolExecutor() as ex: list(ex.map(save, [(reader, i, pages_dir) for i in range(len(reader.pages))]))

DRIP = ADATA / "local" / "bookdrip"   # paced codex transcription queue: rate-capped so one run can't eat the subscription
def _dstate():
    import json
    try: return json.loads((DRIP / "state.json").read_text())
    except Exception: return {}
def _dw(**kw):
    import json
    DRIP.mkdir(parents=True, exist_ok=True); s = _dstate(); s.update(kw); s["ts"] = int(time.time())
    t = DRIP / f".s{os.getpid()}"; t.write_text(json.dumps(s)); t.rename(DRIP / "state.json")
def _dlog(m):
    DRIP.mkdir(parents=True, exist_ok=True)
    open(DRIP / "drip.log", "a").write(f"{time.strftime('%m-%d %H:%M:%S')} {m}\n")
def _dmiss(b):
    from PyPDF2 import PdfReader
    n = len(PdfReader(str(b / "source.pdf")).pages); t = b / "transcriptions"
    return n, [i for i in range(1, n + 1) if not (t / f"page_{i:04d}.txt").exists() or not (t / f"page_{i:04d}.txt").stat().st_size]
ENG_OK = ("codex", "opus", "sonnet", "haiku")   # fable NEVER: interactive quota is not for batch burns
def _dengines():
    e = [x for x in _dstate().get("engines", ["codex", "opus"]) if x in ENG_OK]
    return e or ["codex"]
def _eng_page(eng, pdf, tdir):   # one engine, one page -> transcription text or ""
    if eng == "codex": return transcribe_page(str(pdf), str(tdir))
    try:
        r = subprocess.run(["claude", "--dangerously-skip-permissions", "--print", "--model", eng],
                           input=f"{_TPROMPT}: {pdf}", text=True, capture_output=True, timeout=240)
        t = r.stdout.strip()
        if r.returncode == 0 and t and "API Error" not in t and "content filtering" not in t.lower():
            (Path(tdir) / f"{Path(pdf).stem}.txt").write_text(t if "<transcription>" in t else f"<transcription>\n{t}\n</transcription>")
            return t
    except Exception: pass
    return ""
def _eng_alive(eng):
    if eng != "codex":
        try:
            r = subprocess.run(["claude", "-p", "reply with exactly: READY", "--model", eng, "--dangerously-skip-permissions"], capture_output=True, text=True, timeout=90)
            return "READY" in r.stdout
        except Exception: return False
    return _codex_alive()
def _codex_alive():   # trivial probe: distinguishes quota-out from a poison page
    f = tempfile.NamedTemporaryFile(suffix=".out", delete=False); f.close()
    try:
        subprocess.run(["codex", "exec", "reply with exactly: READY", "--skip-git-repo-check", "-o", f.name], capture_output=True, timeout=90, stdin=subprocess.DEVNULL)
        return "READY" in Path(f.name).read_text()
    except Exception: return False
    finally:
        try: os.unlink(f.name)
        except OSError: pass
def _drip_text(b, rate):   # chunk jobs (translation etc): chunks/*.txt + prompt.txt -> output/en/<chunk>; all done -> output/translation.txt
    en = b / "output" / "en"; en.mkdir(parents=True, exist_ok=True)
    prompt = (b / "prompt.txt").read_text(); chunks = sorted(p.name for p in (b / "chunks").glob("*.txt"))
    eng = next((e for e in _dengines() if e != "codex"), "opus")   # claude engines only: codex call shape is pdf-specific
    miss = [c for c in chunks if not (en / c).exists() or not (en / c).stat().st_size]
    if miss: _dlog(f"{b.name} text start: {len(miss)}/{len(chunks)} to go, {rate}/h ({eng})")
    for c in miss:
        src = (b / "chunks" / c).read_text()
        try:
            r = subprocess.run(["claude", "--dangerously-skip-permissions", "--print", "--model", eng], input=f"{prompt}\n\n{src}", text=True, capture_output=True, timeout=300)
            o = r.stdout.strip() if r.returncode == 0 else ""
        except Exception: o = ""
        if o and len(o) > len(src) // 4 and "API Error" not in o:
            (en / c).write_text(o + "\n"); _dw(book=b.name, page=c, left=len(miss) - miss.index(c) - 1, total=len(chunks), state="running", reason=""); _dlog(f"{b.name} {c} ok ({eng})")
        elif not _eng_alive(eng):
            _dw(state="paused", reason=f"{eng} down (quota?)"); _dlog("PAUSED — reprobe in 30m"); time.sleep(1800); _dw(state="running", reason="")
        else: _dlog(f"{b.name} {c} failed, {eng} alive — deferred")
        time.sleep(3600 / rate)
    left = [c for c in chunks if not (en / c).exists() or not (en / c).stat().st_size]
    if left: _dlog(f"{b.name} text incomplete ({len(left)} deferred) — no assembly"); return
    if chunks and not (b / "output" / "translation.txt").exists():
        (b / "output" / "translation.txt").write_text("\n\n".join((en / c).read_text().strip() for c in chunks) + "\n")
        _dlog(f"{b.name} TEXT COMPLETE {len(chunks)} chunks -> output/translation.txt")
def _drip_loop(tgt, rate):
    os.environ["A_BOOK_CODEX"] = "1"   # codex only: claude content-filters scans
    os.environ["PATH"] = f"{Path.home()}/.local/bin:" + os.environ.get("PATH", "")   # systemd unit PATH lacks ~/.local/bin: codex was FileNotFound -> instant 'empty', read as quota
    books = [resolve_book(tgt)] if tgt != "all" else sorted(d for d in DATA_DIR.glob("[!.]*") if d.is_dir())
    if tgt == "all" and shutil.which("ebook-convert"):   # free pass first: ebook formats -> txt via calibre; codex only for scans
        _dw(book="(converting ebooks)", page="-", total="-", left="-", state="running", reason="calibre pass before codex")
        for b in books:
            src2 = next((s for s in b.glob("source.*") if s.suffix not in (".txt", ".pdf")), None)
            out = b / "output" / (b.name + ".txt")
            if not src2 or out.exists() and out.stat().st_size: continue
            out.parent.mkdir(parents=True, exist_ok=True)
            try: subprocess.run(["ebook-convert", str(src2), str(out)], capture_output=True, timeout=180)
            except Exception: pass
            _dlog(f"{b.name} convert {'ok' if out.exists() and out.stat().st_size else 'failed'}")
    for b in [x for x in books if (x / "chunks").is_dir() and (x / "prompt.txt").is_file()]: _drip_text(b, rate)
    books = [b for b in books if (b / "source.pdf").is_file()]
    _dw(qtotal=len(books), qdone=0)
    fails = 0
    for qi, b in enumerate(books):
        _dw(qdone=qi)
        try: split_pdf(b)
        except Exception as e: _dlog(f"{b.name} split failed {type(e).__name__}"); continue
        total, miss = _dmiss(b)
        if not miss: continue
        _dlog(f"{b.name} start: {len(miss)}/{total} to go, {rate}/h")
        engines = _dengines()
        for n in miss:
            pf = 0   # poison track: page fails while the PRIMARY engine is provably alive (Maxwellians p1: 5m hang -> timeout -> looked like quota, livelocked the queue)
            pdf = b / "pages" / f"page_{n:04d}.pdf"
            if not pdf.is_file():   # split skipped a corrupt page object — engines would strike a ghost (OOTP p1); pdftoppm -f N renders what PyPDF2 can't
                (b / "transcriptions" / f"page_{n:04d}.txt").write_text("<transcription>[source page missing from split — render: pdftoppm -f N -l N source.pdf, then transcribe]</transcription>")
                _dlog(f"{b.name} p{n} no split pdf — placeholder"); continue
            while True:
                used = next((e for e in engines if (_eng_page(e, pdf, b / "transcriptions") or "").strip()), None)
                if used:
                    fails = 0; _dw(book=b.name, page=n, total=total, left=len(_dmiss(b)[1]), state="running", rate=rate, reason=""); _dlog(f"{b.name} p{n} ok ({used})"); break
                if _eng_alive(engines[0]):
                    pf += 1; _dlog(f"{b.name} p{n} failed, {engines[0]} alive ({pf}/2)")
                    if pf >= 2:
                        (b / "transcriptions" / f"page_{n:04d}.txt").write_text("<transcription>[page unreadable — engines failed twice; redo: a book transcribe with --nocache]</transcription>")
                        _dlog(f"{b.name} p{n} POISON — placeholder, moving on"); break
                    time.sleep(30)
                elif any(_eng_alive(e) for e in engines[1:]):
                    _dlog(f"{b.name} p{n} deferred ({engines[0]} down, fallback refused this page)"); break   # no placeholder: retried next pass when primary returns
                else:
                    fails += 1; _dlog(f"{b.name} p{n} empty ({min(fails,3)}/3, all engines down)")   # blank pages return non-empty tags, not a failure
                    if fails >= 3:   # quota-out = silent empties (7/11); park, reprobe, self-resume
                        _dw(state="paused", reason="all engines down (quota?)"); _dlog("PAUSED — reprobe in 30m"); time.sleep(1800); _dw(state="running", reason="")
                    else: time.sleep(60)
            time.sleep(3600 / rate)
        if _dmiss(b)[1]: _dlog(f"{b.name} incomplete ({len(_dmiss(b)[1])} deferred) — no assembly"); continue
        texts = [(b / "transcriptions" / f"page_{i:04d}.txt").read_text().replace("<transcription>", "").replace("</transcription>", "").strip() for i in range(1, total + 1)]
        (b / "output").mkdir(exist_ok=True); (b / "output" / "transcript.txt").write_text("\n\n".join(t for t in texts if t) + "\n")
        _dlog(f"{b.name} COMPLETE {total}p -> output/transcript.txt")
    _dw(state="done"); _dlog("queue done")
def cmd_drip(args):
    sub = args[2] if len(args) > 2 else "status"
    if sub in ("attach", "watch"):   # live stream; Ctrl-C detaches (tail only — never the unit)
        os.execvp("tail", ["tail", "-n", "24", "-f", str(DRIP / "drip.log")])
    if sub == "jobs":   # book = job; pages are drill-down, not the default view
        books = [d for d in sorted(DATA_DIR.glob("[!.]*")) if (d / "source.pdf").is_file()]
        q = args[3] if len(args) > 3 else None
        cur = _dstate().get("book", "")
        if q:
            m = [b for b in books if q.lower() in b.name.lower()]
            if len(m) != 1:
                print("\n".join(b.name for b in m[:20]) or f"no match: {q}"); return
            b = m[0]; t = b / "transcriptions"
            pages = sorted((b / "pages").glob("page_*.pdf")) if (b / "pages").is_dir() else []
            have = {f.stem for f in t.glob("page_*.txt") if f.stat().st_size} if t.is_dir() else set()   # empty-tag blanks count as done
            missing = [p.stem[-4:] for p in pages if p.stem not in have]
            ph = [f.stem[-4:] for f in t.glob("page_*.txt") if t.is_dir() and (b"[page unreadable" in f.read_bytes() or b"[source page missing" in f.read_bytes())]   # marker match, not the word (telegraph prose says unreadable)
            print(f"{b.name}\n  pages {len(have)}/{len(pages) or '?'} done"
                  + (f"\n  missing: {' '.join(missing[:20])}{' …' if len(missing) > 20 else ''}" if missing else "")
                  + (f"\n  placeholders: {' '.join(sorted(ph))}  (redo: a book transcribe <book> N N --nocache)" if ph else ""))
            for l in (DRIP / "drip.log").read_text().splitlines()[::-1]:
                if b.name[:30] in l: print(f"  {l}"); break
            return
        done = part = 0
        rows = []
        for b in books:
            pages = len(list((b / "pages").glob("page_*.pdf"))) if (b / "pages").is_dir() else 0
            t = b / "transcriptions"
            n = sum(1 for f in t.glob("page_*.txt") if f.stat().st_size) if t.is_dir() else 0
            g = "▶" if b.name == cur else "✓" if pages and n >= pages else "◐" if n else "·"
            if g == "✓": done += 1
            if g == "◐": part += 1
            if g != "·" or len(books) <= 30:
                nm = b.name if len(b.name) <= 48 else b.name[:23] + "…" + b.name[-24:]
                rows.append(f" {g} {nm:<48} {n}/{max(pages, n) if pages else '?'}")   # ghost-page txts can exceed split pdfs
        print(f"{done} done · {part} partial · {len(books) - done - part} queued of {len(books)} — drill: a book drip jobs <substr>")
        print("\n".join(rows))
        return
    if sub == "engines":
        if len(args) > 3:
            want = [x.strip() for x in args[3].split(",") if x.strip()]
            bad = [x for x in want if x not in ENG_OK]
            if bad: sys.exit(f"x not allowed: {','.join(bad)} (fable is never batch-burned; options: {','.join(ENG_OK)})")
            _dw(engines=want); print(f"+ engines: {' -> '.join(want)} (takes effect next page)")
        else: print(f"engines: {' -> '.join(_dengines())}  (options: {','.join(ENG_OK)}; fable never)")
        return
    if sub == "stop":
        if not subprocess.run(["systemctl", "--user", "is-active", "-q", "bookdrip.service"]).returncode:
            subprocess.run(["systemctl", "--user", "stop", "bookdrip.service"]); print("stopped bookdrip.service (auto mode still enabled — a book drip auto off to disable)")
        else:
            s = _dstate()
            try: os.killpg(int(s.get("pid", 0)), 15); print(f"stopped pid {s['pid']}")
            except Exception: print("not running")
        _dw(state="stopped"); return
    if sub == "auto":   # persistent mode: systemd --user unit -> starts at boot (linger), Restart=on-failure resumes crashes
        if len(args) > 3 and args[3] == "off":
            subprocess.run(["systemctl", "--user", "disable", "--now", "bookdrip.service"]); _dw(state="stopped"); print("- bookdrip.service disabled"); return
        rate = float(args[3]) if len(args) > 3 else 20.0
        u = Path.home() / ".config/systemd/user/bookdrip.service"; u.parent.mkdir(parents=True, exist_ok=True)
        u.write_text(f"[Unit]\nDescription=a book drip - paced codex transcription of all books\n[Service]\nEnvironment=PATH={Path.home()}/.local/bin:/usr/local/bin:/usr/bin:/bin\nExecStart={Path.home()}/.local/bin/a book drip fg all {rate}\nRestart=on-failure\nRestartSec=120\nNice=10\n[Install]\nWantedBy=default.target\n")
        subprocess.run(["systemctl", "--user", "daemon-reload"]); subprocess.run(["systemctl", "--user", "enable", "--now", "bookdrip.service"])
        print(f"+ bookdrip.service: all books at {rate}/h — survives reboot; status: a book drip; off: a book drip auto off"); return
    if sub in ("start", "fg"):
        if len(args) < 4: sys.exit(f"usage: a book drip {sub} <name|all> [pages/hour]")
        tgt = args[3]; rate = float(args[4]) if len(args) > 4 else 20.0
        if tgt != "all": resolve_book(tgt)   # validate before forking
        s = _dstate()
        if s.get("state") in ("running", "paused"):
            try: os.kill(int(s.get("pid", 0)), 0); sys.exit(f"already running pid {s['pid']} — a book drip stop first")
            except ProcessLookupError: pass
        if sub == "fg":   # unit mode: no fork, SIGTERM = clean stop marker, cache makes every restart a resume
            import signal
            signal.signal(signal.SIGTERM, lambda *a: (_dw(state="stopped"), os._exit(0)))
        else:
            p = os.fork()
            if p: print(f"+ drip pid {p}: {tgt} at {rate} pages/hour — status: a book drip"); return
            os.setsid(); DRIP.mkdir(parents=True, exist_ok=True)
            fd = os.open(DRIP / "out.log", os.O_WRONLY | os.O_CREAT | os.O_APPEND); os.dup2(fd, 1); os.dup2(fd, 2); os.close(0)
        _dw(pid=os.getpid(), target=tgt, rate=rate, state="running", reason="")
        try: _drip_loop(tgt, rate)
        except Exception as e: _dw(state="dead", reason=f"{type(e).__name__}: {e}"); _dlog(f"CRASH {type(e).__name__}: {e}"); os._exit(1)
        os._exit(0)
    s = _dstate()
    if not s: print("no drip yet — a book drip start <name|all> [pages/hour]"); return
    alive = True
    try: os.kill(int(s.get("pid", 0)), 0)
    except Exception: alive = False
    st = s.get("state", "?") + (" (PROCESS DEAD)" if not alive and s.get("state") in ("running", "paused") else "")
    left, rate = s.get("left"), s.get("rate", 0)
    q = f"  queue {s['qdone']}/{s['qtotal']} pdfs" if "qtotal" in s else ""
    print(f"{s.get('target', '?')}{q}  book={s.get('book', '-')} p{s.get('page', '-')}  left={left}/{s.get('total', '?')}  {rate}/h  [{st}] {s.get('reason', '')}"
          + (f"  eta {left / rate:.1f}h" if isinstance(left, int) and rate else ""))
    try: mdl = next(l.split("=", 1)[1].strip().strip('"') for l in (Path.home() / ".codex/config.toml").read_text().splitlines() if l.replace(" ", "").startswith("model="))
    except Exception: mdl = "?"
    print(f"  engines: {' -> '.join(_dengines())} (fable never); codex model {mdl}; set: a book drip engines codex,opus")
    if s.get("state") == "paused": print(f"  next probe ~{time.strftime('%H:%M', time.localtime(s.get('ts', 0) + 1800))}")
    elif s.get("state") == "running" and rate: print(f"  next page ~{time.strftime('%H:%M', time.localtime(s.get('ts', 0) + int(3600 / rate)))}")
    for l in (DRIP / "drip.log").read_text().splitlines()[-4:] if (DRIP / "drip.log").exists() else []: print(f"  {l}")
def cmd_sync():
    remote = "a-gdrive"
    path = f"{remote}:adata/books/"
    # registry upsert FIRST — the list is instantly complete everywhere; content streams up behind it (pull-on-open)
    IDX = ADATA / "git" / "books" / "index.txt"; IDX.parent.mkdir(parents=True, exist_ok=True); IDX.touch()
    rows = [l for l in IDX.read_text().splitlines() if l.strip()]
    have = {r.split("\t")[1] for r in rows if "\t" in r}
    day = time.strftime("%Y-%m-%d")
    new = [f"{path}{d.name}\t{d.name}\t\t{day}\t" for d in sorted(DATA_DIR.iterdir())
           if d.is_dir() and d.name[0] != "." and d.name not in have]
    if new:
        t = IDX.parent / f".idxsync{os.getpid()}"   # atomic: bare write_text races cross-device index writers
        t.write_text("\n".join(rows + new) + "\n"); t.rename(IDX)
        subprocess.run(["flock", "/tmp/.a_git.lock", "sh", "-c",
                        f"cd '{ADATA / 'git'}' && git add books/index.txt && git commit -qm 'books: register {len(new)} (sync upsert)'"], check=False)
    print(f"+ index: {len(new)} registered, {len(rows) + len(new)} total")
    # text only (output/*.txt + source.txt): whole library ~300MB vs tens of GB with scans; devices pull-on-open
    print(f"Syncing book text {DATA_DIR} -> {path}")
    subprocess.run(["rclone", "copy", str(DATA_DIR), path, "--filter", "- .*/**", "--filter", "+ */output/*.txt",
                    "--filter", "+ */source.txt", "--filter", "- *", "--transfers=16", "--progress", "-L"], check=False)
    print(f"Pulling {path} -> {DATA_DIR}")
    subprocess.run(["rclone", "copy", path, str(DATA_DIR), "--progress", "-L", "--ignore-existing"], check=False)

def cmd_add(path):
    p = Path(path)
    if not p.exists(): print(f"File not found: {path}"); sys.exit(1)
    slug = p.stem.lower().replace(" ", "-").replace("_", "-")
    dest = DATA_DIR / slug
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(p), str(dest / f"source{p.suffix}"))
    print(f"Added: {dest}/source{p.suffix}", flush=True)   # exec eats unflushed stdout
    os.execlp("a", "a", "book", "push", slug)   # → push: register + live rclone + url proof

GUT = Path.home() / "civilization/data/gutenberg"   # /civ local dump: 69,999 PG .txt, no network

def _gut_index():
    # id/author/title/lang TSV from the dump files' own headers; one-time ~1min build, then instant
    idx = GUT.parent / "gutenberg-authors.tsv"
    if idx.exists(): return idx
    import re
    print(f"indexing {GUT} (one-time)...", flush=True)
    g = lambda k, h: (lambda m: m[1].strip() if m else "")(re.search(rf"^{k}: *(.+)$", h, re.M))
    rows = []
    for f in GUT.glob("*.txt"):
        h = f.open("rb").read(2048).decode("utf-8", "ignore")
        rows.append(f"{f.stem}\t{g('Author', h)}\t{g('Title', h)}\t{g('Language', h)}")
    idx.write_text("\n".join(rows))
    return idx

def cmd_corpus(author):
    # single-author corpus → adata/corpus/<slug>.txt, for evals on great-person text (GREATS in
    # common/prompts/default.txt; aicombo rung-0 loss). Sources: gutenberg dump (all query words in
    # the Author field, English) + any matching transcribed a-book output/. PG boilerplate stripped.
    import re
    q = author.lower(); works = []
    for line in _gut_index().read_text().splitlines():
        i, a, t, l = (line.split("\t") + [""] * 4)[:4]
        if all(w in a.lower() for w in q.split()) and l.lower().startswith("en") and (GUT / f"{i}.txt").exists():
            x = (GUT / f"{i}.txt").read_text(errors="ignore")
            m = re.search(r"\*\*\* ?START OF.{0,120}?\*\*\*(.*)\*\*\* ?END OF", x, re.S)
            works.append((int(i), t or f"gut {i}", (m[1] if m else x).strip()))
    for d in sorted(DATA_DIR.glob("[!.]*")):
        if all(w in d.name.lower() for w in q.split()) and (d / "output").is_dir():
            for f in sorted((d / "output").glob("*.txt")):
                works.append((0, f"{d.name}/{f.name}", f.read_text(errors="ignore")))
    if not works: sys.exit(f"x no '{author}' in gutenberg index or {DATA_DIR}")
    works.sort(key=lambda w: w[0])
    out = ADATA / "corpus"; out.mkdir(exist_ok=True)
    dst = out / (q.replace(" ", "-") + ".txt")
    dst.write_text("\n\n".join(f"== {t} [{i or 'a book'}] ==\n\n{x}" for i, t, x in works))
    for i, t, _ in works: print(f"  {i or 'book':>6}  {t[:70]}")
    print(f"+ {dst}  {len(works)} works  {sum(len(x.split()) for _, _, x in works):,} words")

def _clean_vtt(p):  # youtube auto-caption vtt → dense prose: drop timing/tags, undo rolling-caption dupes
    import re; out = []
    for l in Path(p).read_text(errors="ignore").splitlines():
        if "-->" in l or l.startswith(("WEBVTT", "Kind:", "Language:")) or not l.strip(): continue
        l = re.sub(r"<[^>]+>", "", l).strip()
        if not l or l in ("[Music]", "[Applause]"): continue
        if out and l == out[-1]: continue                 # exact rolling dup
        if out and l.startswith(out[-1]): out[-1] = l      # partial → fuller line
        else: out.append(l)
    return " ".join(out)

def cmd_yt(great, urls):
    # targeted greats' SPOKEN word (talks/interviews/lectures) → append to adata/corpus/<slug>.txt.
    # Companion to cmd_corpus (their writing); together = one great's full direct-source corpus for evals.
    out = ADATA / "corpus"; out.mkdir(exist_ok=True)
    dst = out / (great.lower().replace(" ", "-") + ".txt"); secs = []
    for u in urls:
        td = tempfile.mkdtemp()
        subprocess.run(["yt-dlp", "--skip-download", "--write-auto-sub", "--sub-lang", "en",
                        "--sub-format", "vtt", "-o", f"{td}/%(title)s.%(ext)s", u],
                       capture_output=True, timeout=600)
        for v in sorted(glob.glob(f"{td}/*.vtt")):
            t = _clean_vtt(v); title = Path(v).name.rsplit(".", 2)[0]
            if len(t.split()) > 50: secs.append((title, t)); print(f"  ✓ {title[:60]} ({len(t.split()):,} words)")
    if not secs: sys.exit("x no captions pulled (private/no-caption video, or yt-dlp blocked)")
    with open(dst, "a") as f:
        for title, t in secs: f.write(f"\n\n== [youtube] {title} ==\n\n{t}")
    print(f"+ appended {len(secs)} transcript(s), {sum(len(t.split()) for _,t in secs):,} words → {dst}")

def _gdrive_info():
    from _common import get_rclone, _configured_remotes
    rc=get_rclone();remotes=_configured_remotes() if rc else []
    if not remotes: return None
    return f"gdrive ({remotes[0]}): https://drive.google.com/drive/search?q=adata%2Fbooks"

def cmd_list(show_all=False):
    books = sorted(p.parent.name for p in DATA_DIR.glob("[!.]*/source.*"))
    if not books: print("No books in", DATA_DIR); return
    limit=len(books) if show_all else 4
    for b in books[:limit]:
        d = DATA_DIR / b
        out = list((d / "output").glob("*.txt")) if (d / "output").exists() else []
        status = f" [{len(out)} outputs]" if out else ""
        print(f"  {b}{status}")
    if not show_all and len(books)>4: print(f"  ... +{len(books)-4} more (a book list)")
    gi=_gdrive_info()
    if gi: print(f"\n  {gi}")
    print("\na book <name>  show book menu\na book add <file>  import PDF\na book sync  cloud sync")

def cmd_show(name):
    book = resolve_book(name)
    from PyPDF2 import PdfReader
    total = len(PdfReader(str(book / "source.pdf")).pages)
    stages = {"pages":"split","transcriptions":"transcribe","translations":"translate","explanations":"explain","output":"output"}
    print(f"\n{book.name}  ({total} pages)\n")
    for d,label in stages.items():
        p = book / d; n = len(list(p.glob("*"))) if p.exists() else 0
        print(f"  {label:<12} {n or '-'}")
    print(f"\n  a book split {name}                    split PDF into pages")
    print(f"  a book transcribe {name} [s e] [w]     OCR pages to text")
    print(f"  a book latex {name} [s e] [w]          OCR pages to LaTeX")
    print(f"  a book translate {name} [lang] [s e w]  translate pages")
    print(f"  a book explain {name} [s e] [w]        annotate obscure terms")
    print(f"  a book chat {name} [files...]           interactive Q&A")
    print(f"\n  s=start e=end page  w=parallel workers  --nocache to redo")

if __name__ == "__main__":
    nocache = "--nocache" in sys.argv
    from_translation = "--from-translation" in sys.argv
    from_transcription = "--from-transcription" in sys.argv
    args = [a for a in sys.argv if not a.startswith("--")]

    if len(args) < 2:
        # tty gets the C pager (lib/book.c, rules: adata/git/mem/tui.md); this path = pipes/help
        print("a book              list TUI: j/k move, spc/b page, a add, o read, c chat, e archive, / filter, q quit\n"
              "a book add <file>   register a local file → upload to a-gdrive:books/, append to index.txt\n"
              "a book push|pull <name>  rclone copy to/from a-gdrive:books/<name>/\n"
              "a book read <name>  open in e -r at saved position; Ctrl-T speaks line; quit saves pos\n"
              "a book chat <name>  interactive Q&A against the book's processed output\n"
              "a book transcribe|translate|explain <name>  OCR / translate / annotate pages\n"
              "a book corpus <author>  single-author .txt → adata/corpus/ (gutenberg dump + outputs), for evals\n"
              "a book yt <great> <url>  append youtube talk/interview transcripts to that great's corpus\n"
              "a book list | index | serve [start|stop] | sync\n"
              "a book archive <substr>  toggle hidden .<name>: saved, not listed")
        sys.exit(0)

    cmd = args[1]
    if cmd == "list": cmd_list(show_all=True)
    elif cmd == "install": subprocess.run(["brew","install","--cask","calibre"] if sys.platform=="darwin" else ["sudo","apt-get","install","-y","calibre"])
    elif cmd == "sync": cmd_sync()
    elif cmd == "drip": cmd_drip(args)
    elif cmd == "archive":  # toggle dot-prefix: data stays, every lister already skips dotdirs
        n = args[2] if len(args) > 2 else sys.exit("Usage: a book archive <substr>")
        m = [DATA_DIR/n] if (DATA_DIR/n).is_dir() else [d for d in DATA_DIR.iterdir() if d.is_dir() and n in d.name]
        if len(m) != 1: sys.exit(f"x {len(m)} matches" + "".join(f"\n  {d.name}" for d in m[:9]))
        t = m[0].with_name(m[0].name[1:] if m[0].name[0] == '.' else '.' + m[0].name)
        o = m[0].name; m[0].rename(t); print("+ restored " + t.name if t.name[0] != '.' else "+ " + t.name)
        subprocess.Popen(["rclone","moveto",f"a-gdrive:adata/books/{o}",f"a-gdrive:adata/books/{t.name}"],
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)  # cloud follows, else any sync pull resurrects the old name
    elif cmd == "lib":
        import json; subprocess.run("pkill -9 -f /opt/calibre;sleep 2",shell=True); p=os.path.expanduser('~/.config/calibre/global.py.json'); json.dump({**json.load(open(p)),'library_path':os.path.expanduser('~/calibre-lib')},open(p,'w'))
    elif cmd == "serve": w=Path.home()/'.local/bin/calibre'; w.exists() or (w.parent.mkdir(parents=True,exist_ok=True),w.write_text('#!/bin/sh\nsystemctl --user stop calibre-server 2>/dev/null\n/usr/bin/calibre "$@"\nsystemctl --user start calibre-server 2>/dev/null\n'),w.chmod(0o755)); subprocess.run(["systemctl","--user","--no-pager",args[2] if len(args)>2 else "status","calibre-server"])
    elif cmd == "add":
        if len(args) < 3: print("Usage: a book add <file>"); sys.exit(1)
        cmd_add(args[2])
    elif cmd == "corpus":
        cmd_corpus(" ".join(args[2:])) if len(args) > 2 else sys.exit("Usage: a book corpus <author>")
    elif cmd == "yt":
        cmd_yt(args[2], args[3:]) if len(args) > 3 else sys.exit("Usage: a book yt <great> <youtube-url|channel|ytsearchN:query> ...")
    elif cmd in ("transcribe", "latex"):
        from PyPDF2 import PdfReader
        fn, cd, sf = (transcribe_page,"transcriptions","transcript") if cmd=="transcribe" else (latex_page,"latex","latex")
        book = resolve_book(args[2] if len(args) > 2 else None)
        split_pdf(book, nocache=nocache)
        total = len(PdfReader(str(book / "source.pdf")).pages)
        start, end = (int(args[3]), int(args[4])) if len(args) >= 5 else (1, total)
        workers = int(args[5]) if len(args) >= 6 else 1
        process_range(book, start, end, fn, "pages", cd, sf, workers, total, nocache=nocache)
    elif cmd == "translate":
        from PyPDF2 import PdfReader
        book = resolve_book(args[2] if len(args) > 2 else None)
        lang = args[3] if len(args) > 3 else input("Language [English]: ") or "English"
        split_pdf(book, nocache=nocache)
        total = len(PdfReader(str(book / "source.pdf")).pages)
        start, end = (int(args[4]), int(args[5])) if len(args) >= 6 else (1, total)
        workers = int(args[6]) if len(args) >= 7 else 1
        process_range(book, start, end, translate_page, "pages", "translations", f"translation-{lang}", workers, total, target_lang=lang, nocache=nocache)
    elif cmd == "explain":
        from PyPDF2 import PdfReader
        book = resolve_book(args[2] if len(args) > 2 else None)
        if not (from_translation or from_transcription): split_pdf(book, nocache=nocache)
        total = len(PdfReader(str(book / "source.pdf")).pages)
        start, end = (int(args[3]), int(args[4])) if len(args) >= 5 else (1, total)
        workers = int(args[5]) if len(args) >= 6 else 1
        source = "translations" if from_translation else ("transcriptions" if from_transcription else "pages")
        process_range(book, start, end, explain_page, source, "explanations", "explained", workers, total, nocache=nocache, technical="--technical" in sys.argv)
    elif cmd == "chat":
        book = resolve_book(args[2] if len(args) > 2 else None)
        out = book / "output"
        txts = sorted(out.glob("*.txt")) if out.exists() else []
        if not txts: print(f"No output in {out}. Run 'a book transcribe' first."); sys.exit(1)
        parts = [f"BOOK: {book.name}\n\n" + "\n\n".join(t.read_text() for t in txts)]
        for md in [ROOT / "AGENTS.md", ROOT / "IDEAS.md"]:
            if md.exists(): parts.append(f"\n\n--- {md.name} ---\n\n" + md.read_text())
        rest = args[3:]; i = rest.index("-p") if "-p" in rest else len(rest)
        ask = rest[i+1:] if i < len(rest) else None  # -p <question>: single-shot, print answer
        for f in rest[:i]:
            p = Path(f)
            if p.exists(): parts.append(f"\n\n--- {p.name} ---\n\n" + p.read_text())
        if not sys.stdin.isatty(): parts.append("\n\n--- PIPED INPUT ---\n\n" + sys.stdin.read())
        tmp = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', prefix='book_chat_', delete=False)
        tmp.write("\n".join(parts)); tmp.close()
        print(f"Context: {os.path.getsize(tmp.name)} bytes from {len(txts)} outputs + extras", file=sys.stderr)
        if ask is not None:
            os.execvp("claude", ["claude", "-p", " ".join(ask), "--dangerously-skip-permissions", "--append-system-prompt-file", tmp.name])
        tty = os.open('/dev/tty', os.O_RDONLY)
        os.dup2(tty, 0); os.close(tty)
        os.execvp("claude", ["claude", "--dangerously-skip-permissions", "--append-system-prompt-file", tmp.name])
    elif cmd == "pos":
        # a book pos <name> <offset> — persist reading char-offset to index.txt col5 (same field `e`/APK use)
        name, off = args[2], (args[3] if len(args) > 3 else "0")
        IDX = ADATA / "git" / "books" / "index.txt"; IDX.parent.mkdir(parents=True, exist_ok=True); IDX.touch()
        out, found = [], False
        for l in IDX.read_text().splitlines():
            p = l.split("\t")
            if len(p) >= 2 and p[1] == name:
                while len(p) < 5: p.append("")
                p[4] = str(off); found = True; out.append("\t".join(p))
            else: out.append(l)
        if not found: out.append("\t".join(["", name, "", "", str(off)]))
        _idxw(IDX, out); _pos_w(name, off)
        print(f"{name} pos {off}")
    elif cmd == "convert":
        # a book convert <name> | <fmt e.g. epub> | all  — calibre ebook-convert source.<ext> -> output/<name>.txt
        if not shutil.which("ebook-convert"): print("install calibre (ebook-convert not found)"); sys.exit(1)
        a = args[2] if len(args) > 2 else "all"
        if (DATA_DIR / a).is_dir(): books, fmt = [DATA_DIR / a], None        # one book by name
        else: books, fmt = sorted(d for d in DATA_DIR.iterdir() if d.is_dir() and d.name[0] != '.'), (None if a == "all" else a.lstrip("."))
        done = skip = fail = 0
        for bd in books:
            src = next(bd.glob("source.*"), None)
            if not src or src.suffix.lower() == ".txt": continue
            if fmt and src.suffix.lower() != "." + fmt: continue
            out = bd / "output" / (bd.name + ".txt")
            if out.exists() and out.stat().st_size: skip += 1; continue
            out.parent.mkdir(parents=True, exist_ok=True)
            try: r = subprocess.run(["ebook-convert", str(src), str(out)], capture_output=True, text=True, timeout=180)
            except subprocess.TimeoutExpired: fail += 1; print(f"✗ {bd.name}: timeout (>180s)"); continue
            if out.exists() and out.stat().st_size: done += 1; print(f"✓ {bd.name}", flush=True)
            else: fail += 1; print(f"✗ {bd.name}: {((r.stderr or r.stdout).strip().splitlines() or ['failed'])[-1][:80]}")
        print(f"\nconverted {done}, skipped {skip} (already had text), failed {fail}")
    elif cmd == "split":
        book = resolve_book(args[2] if len(args) > 2 else None)
        split_pdf(book, nocache=nocache)
        print(f"Split into {book / 'pages'}/")
    elif cmd == "read":
        # open book in `e` editor (read-only) at the saved character offset.
        # exit position written back to adata/git/books/index.txt column 5.
        b = resolve_book(args[2] if len(args) > 2 else None); name = b.name
        txt = b / "output" / "explained.txt"
        if not txt.is_file():
            alt = b / "output" / (name + ".txt")  # a book convert output
            if alt.is_file(): txt = alt
            elif (b / "source.txt").is_file(): txt = b / "source.txt"
            else:  # no text yet — open the source in the native app; prefer .pdf, and confirm (Popen is silent → user can't tell it fired)
                srcs = sorted(b.glob("source.*"), key=lambda p: p.suffix.lower() != ".pdf")
                if not srcs: print(f"no file in {b}"); sys.exit(1)
                f = srcs[0]; print(f">> opening {f.name} in native viewer ({b})")
                subprocess.Popen(["open" if sys.platform=="darwin" else "xdg-open", str(f)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); sys.exit(0)
        IDX = ADATA / "git" / "books" / "index.txt"
        IDX.parent.mkdir(parents=True, exist_ok=True); IDX.touch()
        pos = 0
        lines = IDX.read_text().splitlines()
        for i, l in enumerate(lines):
            parts = l.split("\t")
            if len(parts) >= 2 and parts[1] == name:
                if len(parts) >= 5 and parts[4].strip().isdigit(): pos = int(parts[4])
                break
        pos = _pos_r(name, pos)   # register (seconds-fresh from any device) beats col5 seed
        pos_out = f"/tmp/book_pos_{name}.txt"
        Path(pos_out).unlink(missing_ok=True)
        if shutil.which("e"):
            print(f">> reading {name} from offset {pos} ({txt})")
            subprocess.run(["e", "-r", f"+{pos}", "--pos-out", pos_out, str(txt)])
            new_pos = pos
            try: new_pos = int(Path(pos_out).read_text().strip())
            except Exception: pass
            # write column 5 back, creating row if needed
            updated = False
            for i, l in enumerate(lines):
                parts = l.split("\t")
                if len(parts) >= 2 and parts[1] == name:
                    while len(parts) < 5: parts.append("")
                    parts[4] = str(new_pos)
                    lines[i] = "\t".join(parts); updated = True; break
            if updated: _idxw(IDX, lines)
            _pos_w(name, new_pos)
            print(f"+ position {pos} -> {new_pos} (register + col5)")
            sys.exit(0)
        # APK fallback: narrate via the TTS APK on Android. resumes from /sdcard/Documents/book_pos_<name>.txt
        # large books are sent in ~150KB sessions; re-run to advance.
        import re
        b = resolve_book(args[2] if len(args) > 2 else None); name = b.name
        txt = b / "output" / "explained.txt"
        if txt.is_file(): text = txt.read_text()
        else:
            tx = sorted((b / "transcriptions").glob("*.txt"))
            if not tx: print(f"no text content under {b}/output/ or {b}/transcriptions/"); sys.exit(1)
            text = "\n\n".join(t.read_text() for t in tx)
        on_phone = Path("/data/data/com.termux").is_dir()
        pos_path = Path(f"/sdcard/Documents/book_pos_{name}.txt")
        pos = 0
        if on_phone and pos_path.is_file():
            try: pos = int(pos_path.read_text().split("/")[0])
            except: pos = 0
        elif not on_phone:
            r = subprocess.run(["adb","shell",f"cat {pos_path} 2>/dev/null"], capture_output=True, text=True)
            try: pos = int(r.stdout.split("/")[0])
            except: pos = 0
        sentences = re.split(r"(?<=[.!?])\s+", text)
        total = len(sentences)
        if pos >= total: print(f"+ {name} fully read ({pos}/{total})"); sys.exit(0)
        # push full text via file (debuggable APK + run-as bypasses scoped-storage); pass start=pos
        tmp = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)
        tmp.write(text); tmp.close()
        APK_FILE = "/data/data/com.spatten.ttsdumper/files/book.txt"
        print(f">> {name}: full text {len(text)} chars, resume from sentence {pos}/{total}")
        if on_phone:
            subprocess.run(["cp", tmp.name, "/data/local/tmp/book.txt"], check=False)
        else:
            subprocess.run(["adb","push","-q",tmp.name,"/data/local/tmp/book.txt"], check=False)
        sh = (lambda *xs: subprocess.run(list(xs), check=False)) if on_phone else (lambda *xs: subprocess.run(["adb","shell"] + list(xs), check=False))
        sh("run-as", "com.spatten.ttsdumper", "mkdir", "-p", "files")
        sh("run-as", "com.spatten.ttsdumper", "cp", "/data/local/tmp/book.txt", "files/book.txt")
        sh("am","start","-n","com.spatten.ttsdumper/.HeadlessActivity",
            "--es","file",APK_FILE, "--es","voice","en-gb-x-gbd-network",
            "--ef","pitch","0.48", "--es","book",name, "--ei","start",str(pos))
        Path(tmp.name).unlink(missing_ok=True)
    elif cmd in ("push", "pull", "index"):
        # cross-device library: rclone <-> a-gdrive:books/, line per book in adata/git/books/index.txt
        IDX = ADATA / "git" / "books" / "index.txt"; IDX.parent.mkdir(parents=True, exist_ok=True); IDX.touch()
        RC = "a-gdrive:books"
        if cmd == "index":
            txt = IDX.read_text()
            subprocess.run(["column","-t","-s","\t"], input=txt, text=True) if txt.strip() else print(f"empty — try: a book push <name>")
        elif cmd == "push":
            b = resolve_book(args[2] if len(args) > 2 else None); name = b.name
            # register first so the index reflects intent even if upload is slow/interrupted
            existing = [l for l in IDX.read_text().splitlines() if l.startswith(f"{RC}/{name}\t")]
            if not existing:
                with open(IDX, "a") as f: f.write(f"{RC}/{name}\t{name}\t\t{time.strftime('%Y-%m-%d')}\n")
                print(f"+ registered {name}")
            print(f">> rclone copy {b} → {RC}/{name} (--transfers=20)")
            rc = subprocess.run(["rclone","copy",str(b),f"{RC}/{name}","--transfers=20","--progress"], check=False).returncode
            gid = ""
            try:
                import json; gid = next(x["ID"] for x in json.loads(subprocess.run(["rclone","lsjson",RC,"--dirs-only"],capture_output=True,text=True).stdout) if x["Name"] == name)
            except Exception: pass
            url = f"https://drive.google.com/drive/folders/{gid}" if gid else "https://drive.google.com/drive/search?q=books"
            print(f"{'✓ up' if rc == 0 else 'x rclone rc=%d' % rc}: {RC}/{name} ({','.join(s.name for s in b.glob('source.*')) or 'text only'})\n  {url}")
            if sys.stdin.isatty():   # ARCH 13: show the URL or it didn't happen
                import termios, tty; print("[o]pen url · any key → back", end="", flush=True)
                o = termios.tcgetattr(0); tty.setraw(0)
                try: k = sys.stdin.read(1)
                finally: termios.tcsetattr(0, termios.TCSADRAIN, o); print()
                if k in "oO": subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:  # pull
            if len(args) < 3: print("Usage: a book pull <substr>"); sys.exit(1)
            q = args[2].lower(); m = next((l for l in IDX.read_text().splitlines() if q in l.lower()), None)
            if not m: print(f"no match: {q}"); sys.exit(1)
            src = m.split("\t")[0]; name = src.rsplit("/",1)[-1]
            subprocess.run(["rclone","copy",src,str(DATA_DIR/name),"--progress"], check=False)
            print(f"+ pulled {name} → {DATA_DIR/name}")
    else:
        p = DATA_DIR / cmd
        if p.is_dir() and list(p.glob("source.*")): cmd_show(cmd)
        else: print(f"Unknown: {cmd}"); sys.exit(1)
r'''
#endif
/* a book — C list TUI (rules: adata/git/mem/tui.md); args or no tty → python half above via fallback_py. */
static int bk_cmp(const void*a,const void*b){return strcasecmp((const char*)a,(const char*)b);}
static char bk_nm[4096][128],bk_ad[4096][96],bk_ak[4096][96];   /* name + resolved author display/key */
static int bk_srctag(const char*a){char b[64];int j=0;   /* download-source tag, not an author */
    for(const char*p=a;*p&&j<63;p++)if(isalnum((unsigned char)*p))b[j++]=(char)tolower((unsigned char)*p);b[j]=0;
    return strstr(b,"libgen")||strstr(b,"annasarchive")||strstr(b,"bokxyz")||strstr(b,"zlibrary")||!strcmp(b,"zlib");}
static void bk_norm(const char*a,char*o,int n){char t[12][32];int c=0;   /* alnum tokens lowercased + SORTED */
    for(const char*p=a;*p&&c<12;){while(*p&&!isalnum((unsigned char)*p))p++;if(!*p)break;
        int l=0;while(*p&&isalnum((unsigned char)*p)&&l<31)t[c][l++]=(char)tolower((unsigned char)*p),p++;t[c][l]=0;if(l)c++;}
    for(int i=1;i<c;i++){char x[32];strcpy(x,t[i]);int j=i-1;for(;j>=0&&strcmp(t[j],x)>0;j--)strcpy(t[j+1],t[j]);strcpy(t[j+1],x);}
    int k=0;for(int i=0;i<c&&k<n-2;i++){for(int z=0;t[i][z]&&k<n-2;z++)o[k++]=t[i][z];if(i+1<c)o[k++]=' ';}o[k]=0;}
/* author w/o metadata: tail after last --- (title---author); but if the tail reads like a title (>4 words)
   the name is author---title, so use the head. Same author → same segment → merges (Chernow's mixed forms). */
static const char* bk_auth(const char*nm){const char*f=strstr(nm,"---");if(!f)return "\xc2\xb7 unknown";
    const char*L=0;for(const char*q=nm;(q=strstr(q,"---"));q+=3)L=q;
    const char*t=L+3;while(*t=='-')t++;int w=0;
    for(const char*p=t;*p;){if(*p=='-'){p++;continue;}w++;while(*p&&*p!='-')p++;}
    if(w>4){static char hd[96];int k=(int)(f-nm);if(k>95)k=95;memcpy(hd,nm,(size_t)k);hd[k]=0;return hd;}
    return t;}
static void bk_resolve(char nm[][128],int n){for(int i=0;i<n;i++){const char*a=bk_auth(nm[i]);
    if(!strncmp(a,"\xc2\xb7",2)||!strncmp(a,"unknown",7)||bk_srctag(a)){strcpy(bk_ad[i],"\xc2\xb7 unknown");strcpy(bk_ak[i],"~");}
    else{snprintf(bk_ad[i],96,"%s",a);bk_norm(a,bk_ak[i],96);}}}
static char (*g_ak)[96];   /* set before qsort of an index[] by resolved-author key */
static int g_akcmp(const void*pa,const void*pb){return strcmp(g_ak[*(const int*)pa],g_ak[*(const int*)pb]);}
/* over-long names get middle-… so beginning AND end show (end-truncation would hide the title) */
static void bk_mid(char*s,int w){int l=(int)strlen(s);if(l<=w||w<8)return;int h=(w-3)/2;
    char*e=s+l-(w-3-h);while((*e&0xC0)==(char)0x80)e++;
    memmove(s+h+3,e,strlen(e)+1);memcpy(s+h,"\xe2\x80\xa6",3);}
/* cloud follows local rename (bg moveto) — else sync pull resurrects old name */
static void bk_cloudmv(const char*a,const char*b){if(fork())return;
    int dn=open("/dev/null",O_WRONLY);if(dn>=0){dup2(dn,1);dup2(dn,2);}
    execl("/bin/sh","sh","-c","r=$(rclone listremotes 2>/dev/null|grep a-gdrive|head -1);"
        "[ -n \"$r\" ]&&exec rclone moveto \"${r}adata/books/$0\" \"${r}adata/books/$1\"",a,b,(char*)0);_exit(0);}
static void bk_jget(const char*j,const char*k,char*o,int n){o[0]=0;char pat[24];snprintf(pat,24,"\"%s\":",k);   /* flat one-level state.json only */
    const char*p=strstr(j,pat);if(!p)return;p+=strlen(pat);while(*p==' ')p++;if(*p=='"')p++;   /* json.dumps pads ": " */
    int i=0;while(p[i]&&p[i]!='"'&&p[i]!=','&&p[i]!='}'&&i<n-1){o[i]=p[i];i++;}o[i]=0;}
static void bk_drip(char*o,int n){o[0]=0;char fp[P];snprintf(fp,P,"%s/local/bookdrip/state.json",AROOT);
    size_t l=0;char*j=readf(fp,&l);if(!j)return;
    char st[24],bk[64],pg[12],qd[12],qt[12],pid[16];
    bk_jget(j,"state",st,24);bk_jget(j,"book",bk,64);bk_jget(j,"page",pg,12);
    bk_jget(j,"qdone",qd,12);bk_jget(j,"qtotal",qt,12);bk_jget(j,"pid",pid,16);free(j);
    if(!st[0]||!strcmp(st,"stopped"))return;   /* quiet when intentionally off */
    if((!strcmp(st,"running")||!strcmp(st,"paused"))&&(atoi(pid)<2||kill(atoi(pid),0)))strcpy(st,"DEAD");
    char q[28]="";if(qt[0]&&strcmp(qt,"0"))snprintf(q,28," %s/%s",qd,qt);
    char lb[40];if(!strcmp(st,"running"))strcpy(lb,"transcribing");else snprintf(lb,40,"transcribe %s",st);   /* say the thing, not the codename */
    bk_mid(bk,30);snprintf(o,(size_t)n,"  \xc2\xb7 %s%s %s p%s",lb,q,bk,pg);}
static int cmd_book(int argc,char**argv){
    if(argc>2||!isatty(0)||!isatty(1))fallback_py("book",argc,argv);
    perf_disarm();init_db();signal(SIGCHLD,SIG_IGN);   /* bk_cloudmv children: no zombies while the pager runs */
    char (*nm)[128]=bk_nm;int n=0;char bd[P];snprintf(bd,P,"%s/books",AROOT);
    {DIR*d=opendir(bd);struct dirent*e;struct stat st;char p[P];
     while(d&&(e=readdir(d))&&n<4096){if(e->d_name[0]=='.'||!strcmp(e->d_name,"book.py"))continue;
        snprintf(p,P,"%s/%s",bd,e->d_name);if(!stat(p,&st)&&S_ISDIR(st.st_mode))snprintf(nm[n++],128,"%s",e->d_name);}
     if(d)closedir(d);}
    if(!n){puts("x no books — a book add <file>");return 1;}
    qsort(nm,(size_t)n,128,bk_cmp);
    bk_resolve(nm,n);g_ak=bk_ak;   /* clean authors once; per-keypress reads stay O(1) */
    char ft[64]="";int cur=0,fm=0,na=0,sm=0;static char arc[64][128];   /* sm: name|author sort */
    /* raw mode once (per-key reset would eat omnibox type-ahead) */
    struct termios ot,rt;tcgetattr(0,&ot);rt=ot;rt.c_lflag&=~(tcflag_t)(ICANON|ECHO);rt.c_cc[VMIN]=1;tcsetattr(0,TCSANOW,&rt);
    for(;;){
        static int ix[4096];int m=0;for(int i=0;i<n;i++)if(!*ft||strcasestr(nm[i],ft))ix[m++]=i;
        if(sm)qsort(ix,(size_t)m,sizeof(int),g_akcmp);
        /* author mode: header row before each author run (Lb: book idx, -1=header→Lh) */
        static int Lb[5000];static const char*Lh[5000];int nl=0;char pk[96]="";
        for(int i=0;i<m&&nl<4996;i++){
            if(sm&&strcmp(bk_ak[ix[i]],pk)){Lh[nl]=bk_ad[ix[i]];Lb[nl++]=-1;strcpy(pk,bk_ak[ix[i]]);}
            Lh[nl]=0;Lb[nl++]=ix[i];}
        if(cur>=nl)cur=nl?nl-1:0;
        while(cur<nl&&Lb[cur]<0)cur++;if(cur>=nl){cur=nl-1;while(cur>0&&Lb[cur]<0)cur--;}
        struct winsize w={0,0,0,0};ioctl(1,TIOCGWINSZ,&w);int rows=w.ws_row>10?w.ws_row:24,cols=w.ws_col>20?w.ws_col:80;
        printf("\033[H\033[2J");
        static const char mn[]="[j/k]move [spc/b]page [a]dd [s]ort [o]read [c]chat [t]ranscribe [e]archive [/]filter [q]quit";
        int mr=((int)sizeof(mn)-1+cols-1)/cols;   /* wrapped menu rows (overflow scrolls row1 off) */
        int ps=rows-1-mr,p0=ps>0?(cur/ps)*ps:0;
        for(int i=p0;i<p0+ps&&i<nl;i++){
            if(Lb[i]<0){char h[128];const char*a=Lh[i];int j=0;for(;a[j]&&j<cols-3&&j<120;j++)h[j]=a[j]=='-'?' ':a[j];h[j]=0;
                printf("\033[1;36m%s\033[0m\n",h);continue;}   /* author header */
            char ln[280];snprintf(ln,280,"%s",nm[Lb[i]]);bk_mid(ln,cols-1);
            printf(i==cur?"\033[7m%s\033[0m\n":"%s\n",ln);}
        if(!m)printf("no match: %s\n",ft);
        int cb=0;for(int i=0;i<=cur&&i<nl;i++)if(Lb[i]>=0)cb++;
        /* filter mode swaps menu→typing help: action keys (c=chat…) would type into the search, not fire, so don't show them */
        char ds[96],sr[224];bk_drip(ds,96);   /* live drip segment in the status row; whole row clipped to cols */
        snprintf(sr,224,"%d/%d  %s%s%.*s%s",m?cb:0,m,sm?"by-author  ":"by-name  ",*ft?"filter: ":"",cols>30?cols-30:14,ft,ds);
        printf("\033[%d;1H\033[90m%.*s\033[0m\n%s",rows-mr,cols-1,sr,
            fm?"\033[7;33m SEARCHING \033[0m \033[1m[Enter]\033[0m=pick  [Esc]=cancel":mn);
        fflush(stdout);
        char kc=0;if(read(0,&kc,1)!=1)break;int k=kc,ar=0;
        if(k==27){struct pollfd pf={0,POLLIN,0};   /* arrows = ESC[A/B → k/j (lone ESC stays quit/exit-filter) */
            if(poll(&pf,1,10)>0){char s[2]={0,0};
                if(read(0,s,1)!=1)k=0;
                else if(s[0]=='[')k=read(0,s+1,1)==1&&s[1]=='A'?(ar=1,'k'):s[1]=='B'?(ar=1,'j'):0;
                else k=s[0];}}   /* ESC then a fast real key (or Alt+key): keep the key, drop the ESC */
        if(fm&&!ar){if(k=='\r'||k=='\n'||k==27)fm=0;
            else if(k==127||k==8){size_t l=strlen(ft);if(l)ft[l-1]=0;}
            else if(k>=32&&k<127&&strlen(ft)<63){size_t l=strlen(ft);ft[l]=(char)k;ft[l+1]=0;cur=0;}
            continue;}
        if(k=='q'||k==27)break;
        else if(k=='j'){int t=cur+1;while(t<nl&&Lb[t]<0)t++;if(t<nl)cur=t;}   /* next book, skipping header rows */
        else if(k=='k'){int t=cur-1;while(t>=0&&Lb[t]<0)t--;if(t>=0)cur=t;}
        else if(k==' '&&m){cur+=ps;if(cur>=nl)cur=nl-1;}
        else if(k=='b'&&m){cur-=ps;if(cur<0)cur=0;}
        else if(k=='s'){sm^=1;cur=0;}   /* toggle name <-> author grouping */
        else if(k=='t'||k=='d'){printf("\033[H\033[2J\033[0m");fflush(stdout);   /* transcribe pane: status + log tail + unit; r=resume-now x=stop */
            char dc[B];snprintf(dc,B,"a book drip 2>/dev/null;echo;a book drip jobs 2>/dev/null|head -16;printf 'unit: ';systemctl --user is-active bookdrip.service 2>/dev/null||true");
            (void)!system(dc);
            printf("\n\033[7m [a]ttach live  [r]esume/probe now  [x]stop  [any]back \033[0m");fflush(stdout);
            char t=0;(void)!read(0,&t,1);
            if(t=='a'){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J-- live transcription log, Ctrl-C detaches --\n");fflush(stdout);
                char ac[P];snprintf(ac,P,"%s/local/bookdrip/drip.log",AROOT);execlp("tail","tail","-n","24","-f",ac,(char*)0);}
            if(t=='r'){char rt[12]="20",fp2[P],rc[B];snprintf(fp2,P,"%s/local/bookdrip/state.json",AROOT);   /* keep the configured rate across resume */
                size_t l2=0;char*j2=readf(fp2,&l2);if(j2){bk_jget(j2,"rate",rt,12);free(j2);if(!rt[0])strcpy(rt,"20");}
                snprintf(rc,B,"a book drip auto %s >/dev/null 2>&1;systemctl --user restart bookdrip.service 2>/dev/null;sleep 1",rt);
                (void)!system(rc);}
            else if(t=='x')(void)!system("a book drip stop >/dev/null 2>&1");}
        else if(k=='a'){tcsetattr(0,TCSANOW,&ot);signal(SIGCHLD,SIG_DFL);printf("\033[H\033[2J\033[0m");fflush(stdout);   /* e --pick = the fleet file selector; esc/quit in e → back to menu */
            char pb[P]="",ec[B];snprintf(ec,B,"e --pick '%s/a_bookpick' \"$([ -d \"$HOME/Downloads\" ]&&echo \"$HOME/Downloads\"||echo \"$HOME\")\"",TMP);
            int rc=system(ec);char pf[P];snprintf(pf,P,"%s/a_bookpick",TMP);
            {FILE*f=fopen(pf,"r");if(f){if(fgets(pb,P,f))pb[strcspn(pb,"\n")]=0;fclose(f);unlink(pf);}}
            if(!pb[0]&&rc){tcsetattr(0,TCSANOW,&rt);printf("x e picker unavailable \xe2\x80\x94 use: a book add <file>  [any key]");fflush(stdout);char t=0;(void)!read(0,&t,1);}
            if(pb[0]){char ac[B];snprintf(ac,B,"a book add '%s'",pb);(void)!system(ac);   /* add execs push: rclone + url receipt + key-wait */
                tcsetattr(0,TCSANOW,&ot);execlp("a","a","book",(char*)0);}
            signal(SIGCHLD,SIG_IGN);tcsetattr(0,TCSANOW,&rt);}
        else if(k=='/'){fm=1;ft[0]=0;cur=0;}
        else if((k=='o'||k=='\r'||k=='\n')&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","read",nm[Lb[cur]],0};fallback_py("book",4,av);}
        else if(k=='c'&&m){tcsetattr(0,TCSANOW,&ot);printf("\033[H\033[2J");char*av[]={"a","book","chat",nm[Lb[cur]],0};fallback_py("book",4,av);}
        else if(k=='e'&&m){char fr[P],to[P];int i=Lb[cur];
            snprintf(fr,P,"%s/%s",bd,nm[i]);snprintf(to,P,"%s/.%s",bd,nm[i]);
            if(!rename(fr,to)){char cd[140];snprintf(cd,140,".%s",nm[i]);bk_cloudmv(nm[i],cd);
                snprintf(arc[na<64?na:63],128,"%s",nm[i]);if(na<64)na++;
                memmove(nm[i],nm[i+1],(size_t)(n-1-i)*128);   /* author arrays stay in lockstep */
                memmove(bk_ad[i],bk_ad[i+1],(size_t)(n-1-i)*96);memmove(bk_ak[i],bk_ak[i+1],(size_t)(n-1-i)*96);n--;}}
    }
    tcsetattr(0,TCSANOW,&ot);
    printf("\033[H\033[2J");   /* exit receipts (tui.md rule 2): stat = ground truth, not memory of the rename */
    for(int i=0;i<na;i++){char p[P];struct stat st;snprintf(p,P,"%s/.%s",bd,arc[i]);
        printf(stat(p,&st)?"\033[31m✗ NOT archived: %s\033[0m\n":"✓ archived .%s\n",arc[i]);}
    if(na)printf("  restore: a book archive <substr>\n");
    return 0;}
#if 0
'''
#endif
