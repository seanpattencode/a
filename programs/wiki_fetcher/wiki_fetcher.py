#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
import aios_db
import urllib.request
import json

req = urllib.request.Request("https://en.wikipedia.org/api/rest_v1/page/random/summary", headers={'User-Agent': 'Mozilla/5.0'})
response = urllib.request.urlopen(req)
data = json.loads(response.read().decode())
output = f"{data.get('title', 'Unknown')}: {data.get('extract', 'No extract available')[:200]}..."
job_id = sys.argv[1:2] and sys.argv[1] or None

aios_db.execute("jobs", "UPDATE jobs SET output=?, status='review', updated=CURRENT_TIMESTAMP WHERE id=?", (output, job_id)) or print(output)