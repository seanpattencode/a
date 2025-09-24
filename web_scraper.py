#!/usr/bin/env python3
import argparse
import json
import sqlite3
import requests
from pathlib import Path
from datetime import datetime
from bs4 import BeautifulSoup

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
scraper_config = aios_dir / "scraper_config.json"
scraped_dir = aios_dir / "scraped_data"
scraped_dir.mkdir(exist_ok=True)
events_db = aios_dir / "events.db"

conn = sqlite3.connect(events_db)
conn.execute("""
    CREATE TABLE IF NOT EXISTS events(
        id INTEGER PRIMARY KEY,
        source TEXT,
        target TEXT,
        type TEXT,
        data TEXT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        processed_by TEXT
    )
""")
conn.close()

default_config = {
    "sites": [
        {
            "url": "https://news.ycombinator.com",
            "selector": ".title",
            "name": "hackernews"
        }
    ]
}

config = json.loads(scraper_config.read_text()) if scraper_config.exists() else default_config
scraper_config.write_text(json.dumps(config, indent=2))

def emit_event(target, event_type, data):
    conn = sqlite3.connect(events_db)
    conn.execute(
        "INSERT INTO events(source, target, type, data) VALUES (?, ?, ?, ?)",
        ("web_scraper", target, event_type, json.dumps(data))
    )
    conn.commit()
    conn.close()

def scrape_sites():
    for site in config["sites"]:
        url = site["url"]
        selector = site.get("selector", "p")
        name = site.get("name", url.replace("https://", "").split("/")[0])

        response = requests.get(url, timeout=10)
        soup = BeautifulSoup(response.text, "html.parser")
        elements = soup.select(selector)[:10]

        data = {
            "url": url,
            "timestamp": datetime.now().isoformat(),
            "content": [elem.get_text(strip=True) for elem in elements]
        }

        output_file = scraped_dir / f"{name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        output_file.write_text(json.dumps(data, indent=2))

        prev_file = sorted(scraped_dir.glob(f"{name}_*.json"))
        has_changes = len(prev_file) < 2 or json.loads(prev_file[-2].read_text())["content"] != data["content"]

        if has_changes:
            emit_event("ALL", "site_changed", {"site": name, "url": url})

        print(f"{name}: {'Changed' if has_changes else 'No change'}")

def add_site(url):
    new_site = {
        "url": url,
        "selector": "p",
        "name": url.replace("https://", "").split("/")[0]
    }
    config["sites"].append(new_site)
    scraper_config.write_text(json.dumps(config, indent=2))
    print(f"Added: {url}")

def list_sites():
    for site in config["sites"]:
        print(f"{site['name']}: {site['url']}")

def status_command():
    total_files = len(list(scraped_dir.glob("*.json")))
    sites_count = len(config["sites"])
    print(f"Sites: {sites_count}")
    print(f"Scraped files: {total_files}")

parser = argparse.ArgumentParser(description="AIOS Web Scraper")
subparsers = parser.add_subparsers(dest="command", help="Commands")

scrape_parser = subparsers.add_parser("scrape", help="Scrape all sites")
add_parser = subparsers.add_parser("add", help="Add site to monitor")
add_parser.add_argument("url", help="Site URL")
list_parser = subparsers.add_parser("list", help="List monitored sites")
status_parser = subparsers.add_parser("status", help="Show status")

args = parser.parse_args()

commands = {
    "scrape": scrape_sites,
    "add": lambda: add_site(args.url),
    "list": list_sites,
    "status": status_command
}

command_func = commands.get(args.command, status_command)
command_func()