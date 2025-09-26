#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
import aios_db
import subprocess
import time

aios_db.execute("jobs", "CREATE TABLE IF NOT EXISTS jobs(id INTEGER PRIMARY KEY, name TEXT, status TEXT, output TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")

cmd = sys.argv[1:] and sys.argv[1] or "list"

cmd == "run" and (aios_db.execute("jobs", "INSERT INTO jobs(name, status, output) VALUES ('wiki', 'running', NULL)"), time.sleep(2), subprocess.run(["python3", "-c", "import urllib.request, json, sys; sys.path.append('/home/seanpatten/projects/AIOS/core'); import aios_db; req=urllib.request.Request('https://en.wikipedia.org/api/rest_v1/page/random/summary', headers={'User-Agent': 'Mozilla/5.0'}); data=json.loads(urllib.request.urlopen(req).read().decode()); output=data.get('title', 'Unknown') + ': ' + data.get('extract', 'No extract available')[:200] + '...'; aios_db.execute('jobs', 'UPDATE jobs SET status=?, output=? WHERE id=(SELECT MAX(id) FROM jobs)', ('review', output))"]))

cmd == "accept" and aios_db.execute("jobs", "UPDATE jobs SET status='done' WHERE id=?", (sys.argv[2],))
cmd == "edit" and aios_db.execute("jobs", "UPDATE jobs SET output=? WHERE id=?", (' '.join(sys.argv[3:]), sys.argv[2]))
cmd == "redo" and (aios_db.execute("jobs", "UPDATE jobs SET status='running', output=NULL WHERE id=?", (sys.argv[2],)), time.sleep(2), subprocess.run(["python3", "-c", f"import urllib.request, json, sys; sys.path.append('/home/seanpatten/projects/AIOS/core'); import aios_db; req=urllib.request.Request('https://en.wikipedia.org/api/rest_v1/page/random/summary', headers={{'User-Agent': 'Mozilla/5.0'}}); data=json.loads(urllib.request.urlopen(req).read().decode()); output=data.get('title', 'Unknown') + ': ' + data.get('extract', 'No extract available')[:200] + '...'; aios_db.execute('jobs', 'UPDATE jobs SET status=?, output=? WHERE id=?', ('review', output, {sys.argv[2]}))"]))
cmd == "clear" and aios_db.execute("jobs", "DELETE FROM jobs")

[[print(f"{r[0]}: {r[1]} - {r[2]} - {(r[3] or 'No output')[:50]}...")] for r in aios_db.query("jobs", "SELECT id, name, status, output FROM jobs ORDER BY id DESC LIMIT 20")]