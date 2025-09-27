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
        list(map(self.send_header, ['Content-Type', 'Cache-Control', 'Access-Control-Allow-Origin'], ['text/event-stream', 'no-cache', '*']))
        self.end_headers()

        page = self.headers.get('Referer', '').split('8080')[-1].split('?')[0]
        job_id = self.path.split('job_id=')[-1].split('&')[0] * ('job_id=' in self.path) or ''

        files = {'/terminal': [f'/home/seanpatten/.aios/autollm_output_{job_id}.txt']}.get(page, [str(aios_db.db_path / 'jobs.db')])

        self.wfile.write(f'data: {subprocess.run(["cat"] + files, capture_output=True, text=True).stdout or "Waiting..."}\n\n'.encode())

        proc = subprocess.Popen(['inotifywait', '-m', '-e', 'modify'] + files, stdout=subprocess.PIPE, text=True)
        list(map(lambda _: self.wfile.write(f'data: {subprocess.run(["cat"] + files, capture_output=True, text=True).stdout}\n\n'.encode()), proc.stdout))

HTTPServer(('', 3001), H).serve_forever()