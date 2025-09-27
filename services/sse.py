#!/usr/bin/env python3
import sys
sys.path.append('/home/seanpatten/projects/AIOS')
from http.server import HTTPServer, BaseHTTPRequestHandler
import subprocess
from pathlib import Path
from core import aios_db

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()

        page = self.headers.get('Referer', '').split('8080')[-1].split('?')[0]
        job_id = self.path.split('job_id=')[-1].split('&')[0] * ('job_id=' in self.path) or ''

        def render_autollm():
            worktrees = aios_db.query("autollm", "SELECT branch, path, job_id, status, task, model, output FROM worktrees")
            running = list(filter(lambda w: w[3] == 'running', worktrees))
            review = list(filter(lambda w: w[3] == 'review', worktrees))
            done = list(filter(lambda w: w[3] == 'done', worktrees))

            def fmt_run(w):
                f = Path.home() / ".aios" / f"autollm_output_{w[2]}.txt"
                preview = (f.exists() and f.read_text()[-200:]) or "Waiting..."
                return f'<div class="worktree"><span class="status running">{w[0]}</span><br>{w[5]}: {w[4][:30]}<br><pre style="background:#000;padding:5px;margin:5px 0;max-height:100px;overflow-y:auto;font-size:10px">{preview}</pre><a href="/terminal?job_id={w[2]}">Terminal</a></div>'

            def fmt_rev(w):
                return f'<div class="worktree"><span class="status review">{w[0]}</span><br>{w[5]}: {w[4][:30]}<br>Output: {(w[6] or "")[:50]}<br><form action="/autollm/accept" method="POST"><input type="hidden" name="job_id" value="{w[2]}"><button>Accept</button></form></div>'

            running_html = "".join(list(map(fmt_run, running))) or '<div style="color:#888">No running</div>'
            review_html = "".join(list(map(fmt_rev, review))) or '<div style="color:#888">No review</div>'
            done_html = str(len(done)) + " done" * bool(done) or '<div style="color:#888">No done</div>'

            return f'<div class="grid"><div><h2>Running</h2><div>{running_html}</div></div><div><h2>Review</h2><div>{review_html}</div></div><div><h2>Done</h2><div>{done_html}</div></div></div>'

        files = {'/terminal': [f'/home/seanpatten/.aios/autollm_output_{job_id}.txt'], '/autollm': [str(aios_db.db_path / 'autollm.db')]}.get(page, ['/tmp/dummy'])
        content = render_autollm() * ('/autollm' in page) or ((Path(files[0]).read_text() * Path(files[0]).exists()) * ('/terminal' in page) or "Waiting...")

        self.wfile.write(f'data: {content}\n\n'.encode())
        self.wfile.flush()

        proc = subprocess.Popen(['inotifywait', '-q', '-m', '-e', 'modify'] + files, stdout=subprocess.PIPE, text=True)
        list(map(lambda _: (self.wfile.write(f'data: {render_autollm() * ("/autollm" in page) or ((Path(files[0]).read_text() * Path(files[0]).exists()) * ("/terminal" in page) or "Waiting...")}\n\n'.encode()), self.wfile.flush()), proc.stdout))

HTTPServer(('', 3001), H).serve_forever()