#!/usr/bin/env python3
"""aio demo UI - minimal, friendly, modern (no dependencies)"""
import sys, os, subprocess as sp, json, webbrowser, re
from http.server import HTTPServer, BaseHTTPRequestHandler

HTML = '''<!doctype html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>aio</title>
<style>
:root{--bg:#f8fafc;--card:#fff;--pri:#6366f1;--sec:#8b5cf6;--txt:#1e293b;--mute:#64748b;--bord:#e2e8f0}
*{box-sizing:border-box;margin:0}
body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt);padding:16px;max-width:600px;margin:auto}
h1{font-size:1.5rem;font-weight:600;margin-bottom:16px}
.card{background:var(--card);border-radius:12px;padding:16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}
.row{display:flex;gap:8px;flex-wrap:wrap}
.col{display:flex;flex-direction:column;gap:8px}
button{padding:10px 16px;border:none;border-radius:8px;font-size:14px;cursor:pointer}
.pri{background:var(--pri);color:#fff}
.sec{background:var(--sec);color:#fff}
.out{background:var(--bord);color:var(--txt)}
.ghost{background:transparent;color:var(--pri);border:1px solid var(--bord)}
input,textarea{width:100%;padding:10px;border:1px solid var(--bord);border-radius:8px;font-size:14px}
.proj{display:flex;align-items:center;gap:8px;padding:8px;border-radius:8px;cursor:pointer}
.proj:hover{background:var(--bord)}
.proj .name{flex:1;font-size:14px}
.proj .num{width:24px;height:24px;border-radius:50%;background:var(--bord);font-size:12px;display:flex;align-items:center;justify-content:center}
.agents{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.term{background:#1e1e2e;border-radius:8px;padding:12px;font-family:monospace;font-size:13px;color:#cdd6f4;min-height:100px;max-height:300px;overflow-y:auto;white-space:pre-wrap;margin-top:12px}
.tabs{display:flex;gap:4px;margin-bottom:12px}
.tab{padding:8px 16px;border-radius:8px;background:var(--bord);cursor:pointer;font-size:13px;border:none}
.tab.active{background:var(--pri);color:#fff}
.hidden{display:none!important}
.status{font-size:12px;color:var(--mute);margin-top:8px}
@media(prefers-color-scheme:dark){
  :root{--bg:#0f172a;--card:#1e293b;--txt:#f1f5f9;--mute:#94a3b8;--bord:#334155}
}
</style>
</head>
<body>
<h1>aio</h1>

<div style="background:linear-gradient(135deg,#ff6b35,#f7c59f);border-radius:12px;padding:12px 16px;margin-bottom:12px;display:flex;align-items:center;gap:12px">
  <span style="font-size:32px">🍔</span>
  <div>
    <div style="font-weight:700;color:#fff;font-size:15px">BUGS IN YOUR CODE? PUT A BURGER IN YOUR FACE.</div>
    <div style="color:#fff9;font-size:11px">BigByte Burgers™ — Compile hunger into satisfaction. Side effects may include productivity.</div>
  </div>
</div>

<div class="tabs">
  <button class="tab active" data-tab="main">Main</button>
  <button class="tab" data-tab="git">Git</button>
  <button class="tab" data-tab="notes">Notes</button>
  <button class="tab" data-tab="term">Terminal</button>
</div>

<div id="main" class="page">
  <div class="card">
    <b>Agents</b>
    <div class="agents" style="margin-top:8px">
      <button class="pri" onclick="run('c')">Claude</button>
      <button class="sec" onclick="run('co')">Codex</button>
      <button class="out" onclick="run('g')">Gemini</button>
      <button class="ghost" onclick="run('a')">Aider</button>
    </div>
    <div class="status" id="status"></div>
  </div>

  <div class="card">
    <b>Projects</b>
    <div id="projects" class="col" style="margin-top:8px"></div>
    <div class="row" style="margin-top:8px">
      <button class="ghost" onclick="run('add')" style="flex:1">+ Add current</button>
      <button class="ghost" onclick="run('scan')" style="flex:1">Scan</button>
    </div>
  </div>

  <div class="card">
    <b>Quick</b>
    <div class="row" style="margin-top:8px">
      <button class="out" onclick="run('jobs')" style="flex:1">Jobs</button>
      <button class="out" onclick="run('ls')" style="flex:1">Sessions</button>
      <button class="ghost" onclick="run('x')" style="flex:1">Kill</button>
    </div>
  </div>
</div>

<div id="git" class="page hidden">
  <div class="card">
    <b>Git</b>
    <div class="col" style="margin-top:8px">
      <button class="pri" onclick="run('diff')">Show diff</button>
      <input id="msg" placeholder="Commit message">
      <div class="row">
        <button class="sec" onclick="run('push '+document.getElementById('msg').value)" style="flex:1">Push</button>
        <button class="out" onclick="run('pull')" style="flex:1">Pull</button>
      </div>
    </div>
  </div>
</div>

<div id="notes" class="page hidden">
  <div class="card">
    <b>Quick Note</b>
    <div class="col" style="margin-top:8px">
      <textarea id="note" rows="3" placeholder="Type a note..."></textarea>
      <button class="pri" onclick="addNote()">Add</button>
    </div>
  </div>
  <div class="card">
    <b>Recent</b>
    <div id="notelist" class="col" style="margin-top:8px;font-size:13px"></div>
  </div>
</div>

<div id="term" class="page hidden">
  <div class="card">
    <b>Command</b>
    <div class="row" style="margin-top:8px">
      <input id="cmd" placeholder="aio ..." style="flex:1" onkeydown="if(event.key==='Enter')runCmd()">
      <button class="pri" onclick="runCmd()">Run</button>
    </div>
    <div id="output" class="term">Output appears here</div>
  </div>
</div>

<script>
document.querySelectorAll('.tab').forEach(tab => {
  tab.onclick = () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.page').forEach(p => p.classList.add('hidden'));
    tab.classList.add('active');
    document.getElementById(tab.dataset.tab).classList.remove('hidden');
  };
});

async function api(endpoint, data) {
  const r = await fetch('/api/' + endpoint, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(data || {})
  });
  return r.json();
}

async function load() {
  const d = await api('status');
  document.getElementById('projects').innerHTML = d.projects.map((p, i) =>
    '<div class="proj" onclick="run(\\''+i+'\\')"><span class="num">'+i+'</span><span class="name">'+p.split('/').pop()+'</span></div>'
  ).join('') || '<div style="color:#64748b">No projects</div>';
  document.getElementById('notelist').innerHTML = d.notes.map(n =>
    '<div style="padding:4px 0;border-bottom:1px solid var(--bord)">'+n+'</div>'
  ).join('') || '<div style="color:#64748b">No notes</div>';
}

async function run(cmd) {
  document.getElementById('status').textContent = 'Running: aio ' + cmd;
  const r = await api('run', {cmd: 'aio ' + cmd});
  document.getElementById('output').textContent = r.out || 'Done';
  document.getElementById('status').textContent = '';
  load();
}

async function runCmd() {
  const cmd = document.getElementById('cmd').value;
  if (!cmd) return;
  document.getElementById('output').textContent = 'Running...';
  const r = await api('run', {cmd: cmd.startsWith('aio ') ? cmd : 'aio ' + cmd});
  document.getElementById('output').textContent = r.out || 'Done';
}

async function addNote() {
  const t = document.getElementById('note').value.trim();
  if (!t) return;
  await api('run', {cmd: 'aio n "' + t.replace(/"/g, '\\\\"') + '"'});
  document.getElementById('note').value = '';
  load();
}

load();
</script>
</body></html>'''

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.end_headers()
        self.wfile.write(HTML.encode())

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        data = json.loads(self.rfile.read(length)) if length else {}

        if self.path == '/api/status':
            # Parse projects
            out = sp.getoutput('aio p 2>/dev/null')
            projects = re.findall(r'\d+\. [+x] (.+)', out)
            # Parse notes
            notes = sp.getoutput('sqlite3 ~/.local/share/aios/aio.db "SELECT t FROM notes WHERE s=0 ORDER BY c DESC LIMIT 5" 2>/dev/null').strip().split('\n')
            notes = [n for n in notes if n]
            result = {'projects': projects, 'notes': notes}
        elif self.path == '/api/run':
            out = sp.getoutput(f"source ~/.bashrc 2>/dev/null; {data.get('cmd', '')}")
            result = {'out': out}
        else:
            result = {'error': 'not found'}

        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(result).encode())

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 8081
    url = f'http://localhost:{port}'
    print(f'aio UI running at {url}')
    if os.environ.get('TERMUX_VERSION'):
        sp.run(['termux-open-url', url])
    else:
        webbrowser.open(url)
    HTTPServer(('127.0.0.1', port), Handler).serve_forever()
