#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
(aios_dir / "components").mkdir(exist_ok=True)
(aios_dir / "scraped_data").mkdir(exist_ok=True)
backup_dir = Path.home() / ".aios_backup"
backup_dir.mkdir(exist_ok=True)

config_file = aios_dir / "llm_config.json"
services_file = aios_dir / "services.json"
scraper_config = aios_dir / "scraper_config.json"
goals_file = aios_dir / "goals.txt"
ideas_file = aios_dir / "ideas.txt"
tasks_file = aios_dir / "tasks.txt"

def setup_apis():
    openai_key = input("OpenAI API key (or press Enter to skip): ")
    anthropic_key = input("Anthropic API key (or press Enter to skip): ")
    config = {"api_key": openai_key, "openai_key": openai_key, "anthropic_key": anthropic_key}
    config_file.write_text(json.dumps(config, indent=2))
    print("API keys configured")

def setup_services():
    services = {
        "backup": {
            "unit": "aios-backup.service",
            "exec": f"/usr/bin/python3 {Path.cwd()}/backup_local.py backup"
        },
        "scraper": {
            "unit": "aios-scraper.timer",
            "exec": f"/usr/bin/python3 {Path.cwd()}/web_scraper.py scrape"
        },
        "planner": {
            "unit": "aios-planner.service",
            "exec": f"/usr/bin/python3 {Path.cwd()}/daily_planner.py plan"
        }
    }
    services_file.write_text(json.dumps(services, indent=2))
    print("Services configured")

def setup_data():
    goals_file.write_text("WEEK: Launch AIOS system\nMONTH: Scale to production\nYEAR: Full automation")
    ideas_file.write_text("Build AI assistant\nAutomate daily planning\nCreate voice interface")
    tasks_file.write_text("[ ] 2025-01-23 09:00 p:high Setup AIOS system\n[ ] 2025-01-23 10:00 p:med Test all components")

    scraper_sites = {
        "sites": [
            {"url": "https://news.ycombinator.com", "selector": ".title", "name": "hackernews"},
            {"url": "https://httpbin.org/json", "selector": "p", "name": "httpbin"}
        ]
    }
    scraper_config.write_text(json.dumps(scraper_sites, indent=2))
    print("Sample data created")

def setup_dependencies():
    packages = ["openai", "anthropic", "flask", "beautifulsoup4", "requests",
                "google-api-python-client", "google-auth-httplib2", "google-auth-oauthlib"]
    subprocess.run(["pip", "install"] + packages)
    print("Dependencies installed")

def setup_permissions():
    scripts = Path.cwd().glob("*.py")
    for script in scripts:
        script.chmod(0o755)
    print("Scripts made executable")

def setup_all():
    setup_permissions()
    setup_dependencies()
    setup_apis()
    setup_services()
    setup_data()
    print("\nAIOS setup complete!")

def setup_minimal():
    setup_permissions()
    setup_data()
    print("Minimal setup complete")

def setup_reset():
    for item in aios_dir.iterdir():
        item.unlink() if item.is_file() else None
    print("Reset complete")

def setup_check():
    checks = [
        ("Directory", aios_dir.exists()),
        ("API Config", config_file.exists()),
        ("Services", services_file.exists()),
        ("Goals", goals_file.exists())
    ]
    for name, status in checks:
        print(f"{name}: {'✓' if status else '✗'}")

parser = argparse.ArgumentParser(description="AIOS Setup")
subparsers = parser.add_subparsers(dest="command", help="Commands")

all_parser = subparsers.add_parser("all", help="Complete setup")
minimal_parser = subparsers.add_parser("minimal", help="Minimal setup")
reset_parser = subparsers.add_parser("reset", help="Reset configuration")
check_parser = subparsers.add_parser("check", help="Check setup status")

args = parser.parse_args()

commands = {
    "all": setup_all,
    "minimal": setup_minimal,
    "reset": setup_reset,
    "check": setup_check
}

command_func = commands.get(args.command, setup_check)
command_func()