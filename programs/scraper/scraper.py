#!/usr/bin/env python3
import sys
sys.path.append('/home/seanpatten/projects/AIOS')
import requests
from bs4 import BeautifulSoup
import aios_db
from datetime import datetime

config = aios_db.read("scraper") or {"urls": ["https://news.ycombinator.com"]}
results = []

[results.append({
    "url": url,
    "title": BeautifulSoup(requests.get(url).text, 'html.parser').title.string if BeautifulSoup(requests.get(url).text, 'html.parser').title else "No title",
    "time": datetime.now().isoformat()
}) for url in config.get("urls", [])]

aios_db.write("scraper_results", results)
[print(f"{r['url']}: {r['title']}") for r in results]