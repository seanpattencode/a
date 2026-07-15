# /// script
# dependencies = ["PyPDF2"]
# ///
import sys, subprocess, time, tempfile, os, shutil
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

sys.argv = sys.argv[1:]  # shift: a prepends "book" to argv
ROOT = Path(__file__).resolve().parent.parent
ADATA = ROOT / "adata"
DATA_DIR = ADATA / "books"

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
    import glob
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

def transcribe_page(source_path, output_dir, nocache=False):
    return process_page(source_path, output_dir, "Read this file and transcribe it. Remove headers, footers, page numbers, and section labels like 'INTRODUCTION xix'. For any graphs/charts/images, include a description in the format 'Graph: [description]'. If the page is blank or has no meaningful content, return empty <transcription></transcription> tags. Return ONLY the main body text wrapped in <transcription></transcription> tags", nocache)

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
    import shutil; shutil.copy2(str(p), str(dest / f"source{p.suffix}"))
    print(f"Added: {dest}")

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
    import subprocess, tempfile, glob
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
        print("a book              list TUI: j/k move, spc/b page, o read, c chat, e archive, / filter, q quit\n"
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
        IDX.write_text("\n".join(out) + "\n")
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
        import shutil
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
            if updated:
                IDX.write_text("\n".join(lines) + "\n")
                print(f"+ position {pos} -> {new_pos} (saved to {IDX.name})")
            sys.exit(0)
        # APK fallback: narrate via the TTS APK on Android. resumes from /sdcard/Documents/book_pos_<name>.txt
        # large books are sent in ~150KB sessions; re-run to advance.
        import base64, re
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
        import tempfile
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
            subprocess.run(["rclone","copy",str(b),f"{RC}/{name}","--transfers=20","--progress"], check=False)
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
