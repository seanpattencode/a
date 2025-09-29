#!/usr/bin/env python3
import asyncio, websockets, json, pty, os, struct, fcntl, termios
from threading import Thread
from http.server import HTTPServer, BaseHTTPRequestHandler

HTML = b'''<!DOCTYPE html>
<html>
<head>
<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.css"/>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.js"></script>
<style>body{margin:0;background:#000;}</style>
</head>
<body>
<div id="terminal"></div>
<script>
const term = new Terminal({cursorBlink: true});
const fitAddon = new FitAddon.FitAddon();
term.loadAddon(fitAddon);
term.open(document.getElementById('terminal'));
fitAddon.fit();

const ws = new WebSocket('ws://localhost:9877');
ws.onopen = () => {
  ws.onmessage = e => term.write(JSON.parse(e.data).d);
  term.onData(data => ws.send(JSON.stringify({i: data})));
  term.onResize(({rows, cols}) => ws.send(JSON.stringify({resize: {rows, cols}})));
};

window.onresize = () => fitAddon.fit();
</script>
</body>
</html>'''

class H(BaseHTTPRequestHandler):
    def do_GET(s): s.send_response(200); s.end_headers(); s.wfile.write(HTML)
    def log_message(s,*a): pass

async def client_handler(ws):
    # Create real PTY
    master, slave = pty.openpty()

    # Set terminal size
    winsize = struct.pack('HHHH', 24, 80, 0, 0)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, winsize)

    proc = await asyncio.create_subprocess_exec(
        '/bin/bash',
        stdin=slave,
        stdout=slave,
        stderr=slave,
        preexec_fn=os.setsid
    )

    os.close(slave)  # Close slave in parent

    # Make non-blocking
    os.set_blocking(master, False)

    async def read_pty():
        loop = asyncio.get_event_loop()

        def reader_callback():
            try:
                data = os.read(master, 65536)
                if data:
                    asyncio.create_task(ws.send(json.dumps({'d': data.decode('utf-8', 'replace')})))
            except OSError:
                loop.remove_reader(master)

        loop.add_reader(master, reader_callback)

        # Wait for process to finish
        await proc.wait()
        loop.remove_reader(master)
        os.close(master)

    read_task = asyncio.create_task(read_pty())

    try:
        async for msg in ws:
            data = json.loads(msg)
            if 'i' in data:  # Input
                os.write(master, data['i'].encode())
            elif 'resize' in data:  # Terminal resize
                size = data['resize']
                winsize = struct.pack('HHHH', size['rows'], size['cols'], 0, 0)
                fcntl.ioctl(master, termios.TIOCSWINSZ, winsize)
    except:
        pass
    finally:
        proc.terminate()
        await read_task
        try:
            os.close(master)
        except:
            pass

async def main():
    Thread(target=lambda: HTTPServer(('', 9088), H).serve_forever(), daemon=True).start()
    print("PTY+xterm.js terminal at http://localhost:9088")
    async with websockets.serve(client_handler, 'localhost', 9877):
        await asyncio.Future()

asyncio.run(main())