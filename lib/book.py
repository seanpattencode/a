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
    books = sorted(p.parent.name for p in DATA_DIR.glob("*/source.*"))
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
    subprocess.run(["pdftoppm","-png","-r","100",str(pdf),tmp],capture_output=True)
    pngs = sorted(glob.glob(f"{tmp}*.png"))
    if not pngs: return ""
    of = f"{tmp}.out"
    args = ["codex","exec",prompt,"--skip-git-repo-check","-o",of]
    for p in pngs: args += ["--image",p]
    subprocess.run(args,capture_output=True,timeout=300)
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
    return process_page(source_path, output_dir, f"Translate this document to {target_lang}. Return only the translated text, preserving paragraph structure", nocache)

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
    print(f"Syncing {DATA_DIR} -> {path}")
    subprocess.run(["rclone", "sync", str(DATA_DIR), path, "--progress", "-L"], check=False)
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

def _gdrive_info():
    from _common import get_rclone, _configured_remotes
    rc=get_rclone();remotes=_configured_remotes() if rc else []
    if not remotes: return None
    return f"gdrive ({remotes[0]}): https://drive.google.com/drive/search?q=adata%2Fbooks"

def cmd_list(show_all=False):
    books = sorted(p.parent.name for p in DATA_DIR.glob("*/source.*"))
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
        # omnibox: live filter over (1) books in index.txt, (2) the other subcommands.
        # Selecting a book → read it. Selecting an action → prints its usage.
        IDX = ADATA / "git" / "books" / "index.txt"
        rows = [l for l in (IDX.read_text().splitlines() if IDX.exists() else []) if l.strip()]
        actions = [
            ("a book add <file>",        "register a local file → upload to a-gdrive:books/, append to index.txt"),
            ("a book push <name>",       "rclone copy adata/books/<name>/ to a-gdrive:books/<name>/"),
            ("a book pull <substr>",     "rclone copy a-gdrive:books/<name>/ back to local adata/books/"),
            ("a book read <name>",       "open in e -r at saved position; Ctrl-T speaks line; quit saves pos"),
            ("a book index",             "print the books index (column-aligned)"),
            ("a book transcribe <name>", "OCR PDF pages to text (calibre + claude)"),
            ("a book translate <name>",  "translate pages to target language"),
            ("a book explain <name>",    "annotate obscure terms in each page"),
            ("a book chat <name>",       "interactive Q&A against the book's processed output"),
            ("a book serve [start|stop]","calibre server on this device"),
            ("a book sync",              "rclone bidirectional sync of adata/books/"),
        ]
        pos_by = {p[1]: ((p[4].strip() if len(p) >= 5 else "") or "0") for l in rows for p in [l.split("\t")] if len(p) >= 2}
        local = [d.name for d in DATA_DIR.iterdir() if d.is_dir()] if DATA_DIR.exists() else []
        items = []
        for name in sorted(set(pos_by) | set(local)):
            items.append(f"📖 {name}\topen book (pos {pos_by.get(name, '0')})\tread")
            items.append(f"🧠 {name}\tload to claude\tchat")
        for label, desc in actions:
            items.append(f"⚙  {label}\t{desc}\tact")
        if not shutil.which("fzf"):
            for i, it in enumerate(items):
                cols = it.split("\t"); print(f"  {i:2}. {cols[0]:<40} {cols[1]}")
            try: cols = items[int(input("# "))].split("\t")
            except: sys.exit(0)
        else:
            prev = f'n=$(echo {{1}}|sed "s/^[^ ]* //"); head -c 4000 "{DATA_DIR}/$n/output/"*.txt 2>/dev/null||echo "(no text — a book transcribe $n)"'
            r = subprocess.run(["fzf","--height","80%","--reverse","--prompt","a book > ","--with-nth","1,2","--delimiter","\t","--info","inline",
                "--bind","left:up,right:down","--preview",prev,"--preview-window","right:50%:wrap",
                "--header","←/→ flip books · Enter selects · Esc cancels","--ansi"],
                input="\n".join(items), capture_output=True, text=True)
            if r.returncode != 0: sys.exit(0)
            cols = r.stdout.strip().split("\t")
        if cols[-1] in ("read", "chat"):
            sys.argv = sys.argv[:1] + [cols[-1], cols[0].split(" ", 1)[1].strip()]
            args = [a for a in sys.argv if not a.startswith("--")]
        else:
            print(cols[0].lstrip("⚙ ").strip()); print(f"  {cols[1]}"); sys.exit(0)

    cmd = args[1]
    if cmd == "list": cmd_list(show_all=True)
    elif cmd == "install": subprocess.run(["brew","install","--cask","calibre"] if sys.platform=="darwin" else ["sudo","apt-get","install","-y","calibre"])
    elif cmd == "sync": cmd_sync()
    elif cmd == "lib":
        import json; subprocess.run("pkill -9 -f /opt/calibre;sleep 2",shell=True); p=os.path.expanduser('~/.config/calibre/global.py.json'); json.dump({**json.load(open(p)),'library_path':os.path.expanduser('~/calibre-lib')},open(p,'w'))
    elif cmd == "serve": w=Path.home()/'.local/bin/calibre'; w.exists() or (w.parent.mkdir(parents=True,exist_ok=True),w.write_text('#!/bin/sh\nsystemctl --user stop calibre-server 2>/dev/null\n/usr/bin/calibre "$@"\nsystemctl --user start calibre-server 2>/dev/null\n'),w.chmod(0o755)); subprocess.run(["systemctl","--user","--no-pager",args[2] if len(args)>2 else "status","calibre-server"])
    elif cmd == "add":
        if len(args) < 3: print("Usage: a book add <file>"); sys.exit(1)
        cmd_add(args[2])
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
            src = b / "source.txt"
            if src.is_file(): txt = src
            else: print(f"no readable text in {b}"); sys.exit(1)
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
