from flask import Flask, request
import subprocess, time
app = Flask(__name__)

@app.route('/', methods=['GET', 'POST'])
def index():
    out, t = '', 0
    if request.method == 'POST':
        s = time.time(); out = subprocess.run(request.form['cmd'], shell=True, capture_output=True, text=True).stdout; t = time.time() - s
    return f'<form method=post><input name=cmd value="ls"><button>Run</button></form><pre>{out}</pre><p>{t:.3f}s</p>'

app.run(debug=True)
