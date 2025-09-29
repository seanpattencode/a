#!/usr/bin/env python3
import asyncio, websockets, json, pty, os, struct, fcntl, termios
from http.server import HTTPServer, BaseHTTPRequestHandler
from threading import Thread

HTML = b'''<!DOCTYPE html>
<script src="https://cdn.jsdelivr.net/npm/xterm/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit/lib/xterm-addon-fit.js"></script>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm/css/xterm.css"/>
<style>body{margin:0;background:#000;overflow:hidden}#terminal{height:100vh}</style>
<div id="terminal"></div>
<script>
const term = new Terminal();
const fitAddon = new FitAddon.FitAddon();
term.loadAddon(fitAddon);
term.open(document.getElementById('terminal'));
fitAddon.fit();
const ws = new WebSocket('ws://localhost:8766');
ws.onmessage = e => term.write(JSON.parse(e.data).d);
term.onData(data => ws.send(JSON.stringify({i: data})));
term.onResize(({rows, cols}) => ws.send(JSON.stringify({resize: {rows, cols}})));
window.addEventListener('resize', () => fitAddon.fit());
</script>'''

class HTTPHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(HTML)
    def log_message(self, *args): pass

async def client_handler(ws):
    master, slave = pty.openpty()

    # Set terminal size
    winsize = struct.pack('HHHH', 24, 80, 0, 0)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, winsize)

    proc = await asyncio.create_subprocess_exec(
        'bash', stdin=slave, stdout=slave, stderr=slave,
        preexec_fn=os.setsid
    )
    os.close(slave)

    # Non-blocking
    os.set_blocking(master, False)

    loop = asyncio.get_event_loop()

    def read_output():
        try:
            data = os.read(master, 65536)
            if data:
                asyncio.create_task(ws.send(json.dumps({'d': data.decode('utf-8', 'replace')})))
        except (OSError, BlockingIOError):
            pass

    loop.add_reader(master, read_output)

    try:
        async for msg in ws:
            data = json.loads(msg)
            if 'i' in data:  # Input
                os.write(master, data['i'].encode())
            elif 'resize' in data:  # Terminal resize
                size = data['resize']
                winsize = struct.pack('HHHH', size['rows'], size['cols'], 0, 0)
                fcntl.ioctl(master, termios.TIOCSWINSZ, winsize)
    finally:
        loop.remove_reader(master)
        proc.terminate()
        await proc.wait()
        os.close(master)

async def main():
    Thread(target=lambda: HTTPServer(('', 8700), HTTPHandler).serve_forever(), daemon=True).start()
    print("Terminal: http://localhost:8700")
    async with websockets.serve(client_handler, 'localhost', 8766):
        await asyncio.Future()

if __name__ == '__main__':
    asyncio.run(main())