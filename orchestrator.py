#!/usr/bin/env python3
import datetime
import importlib.util
import json
import logging
import os
import sqlite3
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT_DIR = Path(__file__).parent.absolute()
COMMON_DIR = ROOT_DIR / "Common"
PROGRAMS_DIR = ROOT_DIR / "Programs"
RESULTS_DIR = COMMON_DIR / "Results"
LOGS_DIR = COMMON_DIR / "Logs"
STATE_DB = COMMON_DIR / "orchestrator.db"
TRIGGER_DIR = COMMON_DIR / "Triggers"
TRIGGER_PROCESSED_DIR = COMMON_DIR / "TriggersProcessed"

DEVICE_ID = os.environ.get("DEVICE_ID", str(os.getpid()))
DEVICE_TAGS = set(os.environ.get("DEVICE_TAGS", "").split(",")) if os.environ.get("DEVICE_TAGS") else set()

for dir_path in [COMMON_DIR, PROGRAMS_DIR, RESULTS_DIR, LOGS_DIR, TRIGGER_DIR, TRIGGER_PROCESSED_DIR]:
    dir_path.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.FileHandler(LOGS_DIR / f"orchestrator_{DEVICE_ID}_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.log"),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

SCHEDULED_JOBS = [
    {
        "name": "web_server_daemon",
        "file": "web_server.py",
        "function": "run_server",
        "type": "always",
        "tags": ["browser"],
        "docker": True,
        "retries": 999
    },
    {
        "name": "stock_monitor",
        "file": "stock_monitor.py",
        "function": "monitor_stocks",
        "type": "always",
        "tags": ["gpu"],
        "docker": True,
        "retries": 999
    },
    {
        "name": "morning_report",
        "file": "reports.py",
        "function": "generate_morning_report",
        "type": "daily",
        "time": "09:00",
        "tags": [],
        "docker": False
    },
    {
        "name": "random_check",
        "file": "health_check.py",
        "function": "random_health_check",
        "type": "random_daily",
        "after_time": "14:00",
        "before_time": "18:00",
        "tags": [],
        "docker": True
    },
    {
        "name": "backup_data",
        "file": "backup.py",
        "function": "backup_all",
        "type": "interval",
        "interval_minutes": 60,
        "tags": ["storage"],
        "docker": True,
        "retries": 3
    },
    {
        "name": "llm_processor",
        "file": "llm_tasks.py",
        "function": "process_llm_queue",
        "type": "trigger",
        "tags": ["gpu"],
        "docker": True
    },
    {
        "name": "idle_baseline",
        "file": "idle_task.py",
        "function": "run_idle",
        "type": "idle",
        "tags": [],
        "docker": False,
        "priority": -1
    }
]

class JobState:
    def __init__(self):
        self.lock = threading.Lock()
        self.conn = sqlite3.connect(STATE_DB, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        with self.conn:
            self.conn.execute("PRAGMA journal_mode=WAL;")
            self.conn.execute("""
                CREATE TABLE IF NOT EXISTS jobs (
                    job_name TEXT PRIMARY KEY,
                    status TEXT NOT NULL,
                    device TEXT NOT NULL,
                    last_update REAL NOT NULL
                )
            """)
            self.conn.execute("""
                CREATE TABLE IF NOT EXISTS history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    job_name TEXT NOT NULL,
                    device TEXT NOT NULL,
                    success INTEGER NOT NULL,
                    duration_ms REAL NOT NULL,
                    timestamp REAL NOT NULL
                )
            """)

    def update_job(self, job_name, status):
        with self.lock:
            with self.conn:
                self.conn.execute("""
                    INSERT INTO jobs (job_name, status, device, last_update)
                    VALUES (?, ?, ?, ?)
                    ON CONFLICT(job_name) DO UPDATE SET
                        status=excluded.status,
                        device=excluded.device,
                        last_update=excluded.last_update
                """, (job_name, status, DEVICE_ID, time.time()))

    def log_completion(self, job_name, success, duration_ms):
        with self.lock:
            with self.conn:
                self.conn.execute("""
                    INSERT INTO history (job_name, device, success, duration_ms, timestamp)
                    VALUES (?, ?, ?, ?, ?)
                """, (job_name, DEVICE_ID, int(success), duration_ms, time.time()))

    def get_last_run(self, job_name):
        with self.lock:
            cursor = self.conn.execute("SELECT last_update FROM jobs WHERE job_name = ?", (job_name,))
            row = cursor.fetchone()
            return row["last_update"] if row else None

state = JobState()

def should_run_daily_job(job, last_run):
    target_hour, target_minute = map(int, job["time"].split(":"))
    now = datetime.datetime.now()
    current_minutes = now.hour * 60 + now.minute
    target_minutes = target_hour * 60 + target_minute

    if current_minutes < target_minutes:
        return False

    if last_run and datetime.datetime.fromtimestamp(last_run).date() == datetime.date.today():
        return False

    return True

def should_run_random_daily_job(job, last_run):
    if last_run and datetime.datetime.fromtimestamp(last_run).date() == datetime.date.today():
        return False

    after_hour, after_minute = map(int, job.get("after_time", "00:00").split(":"))
    before_hour, before_minute = map(int, job.get("before_time", "23:59").split(":"))
    now = datetime.datetime.now()
    current_minutes = now.hour * 60 + now.minute

    if current_minutes < after_hour * 60 + after_minute or current_minutes > before_hour * 60 + before_minute:
        return False

    return __import__('random').random() < 0.01

def should_run_interval_job(job, last_run):
    if not last_run:
        return True
    return (time.time() - last_run) / 60 >= job["interval_minutes"]

def load_and_call_function(file_path, function_name, *args, **kwargs):
    script_path = PROGRAMS_DIR / file_path
    if not script_path.exists():
        script_path.parent.mkdir(parents=True, exist_ok=True)
        script_path.write_text(f'''
def {function_name}(*args, **kwargs):
    print(f"{function_name} called")
    return "{function_name} completed"
''')

    spec = importlib.util.spec_from_file_location("module", script_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    if hasattr(module, function_name):
        return getattr(module, function_name)(*args, **kwargs)
    return None

def run_job(job, *args, **kwargs):
    start_time = time.time()
    try:
        result = load_and_call_function(job["file"], job["function"], *args, **kwargs)
        logger.info(f"Job {job['name']} completed with result: {result}")
        return True, (time.time() - start_time) * 1000
    except Exception as e:
        logger.error(f"Job {job['name']} failed: {e}")
        return False, (time.time() - start_time) * 1000

def run_job_with_retry(job, *args, **kwargs):
    max_retries = job.get("retries", 3)
    for attempt in range(max_retries):
        state.update_job(job["name"], "running")
        success, duration_ms = run_job(job, *args, **kwargs)
        state.log_completion(job["name"], success, duration_ms)

        if success:
            state.update_job(job["name"], "completed")
            return True

        if attempt < max_retries - 1:
            time.sleep(2 ** attempt)

    state.update_job(job["name"], "failed")
    return False

class JobScheduler:
    def __init__(self):
        self.running_jobs = {}
        self.executor = ThreadPoolExecutor(max_workers=10)
        self.stop_event = threading.Event()

    def start_job(self, job, *args, **kwargs):
        if job["name"] in self.running_jobs and self.running_jobs[job["name"]].is_alive():
            return

        def job_wrapper():
            try:
                logger.info(f"Starting job: {job['name']}")
                run_job_with_retry(job, *args, **kwargs)
            finally:
                self.running_jobs.pop(job["name"], None)

        thread = threading.Thread(target=job_wrapper, daemon=True)
        self.running_jobs[job["name"]] = thread
        thread.start()

    def check_scheduled_jobs(self):
        non_idle_running = any(
            job["name"] in self.running_jobs and self.running_jobs[job["name"]].is_alive()
            for job in SCHEDULED_JOBS if job["type"] != "idle"
        )

        for job in SCHEDULED_JOBS:
            required_tags = set(job.get("tags", []))
            if required_tags and not required_tags.issubset(DEVICE_TAGS):
                continue

            job_name = job["name"]
            last_run = state.get_last_run(job_name)
            should_run = False

            if job["type"] == "always" and job_name not in self.running_jobs:
                should_run = True
            elif job["type"] == "daily":
                should_run = should_run_daily_job(job, last_run)
            elif job["type"] == "random_daily":
                should_run = should_run_random_daily_job(job, last_run)
            elif job["type"] == "interval":
                should_run = should_run_interval_job(job, last_run)
            elif job["type"] == "idle" and not non_idle_running and job_name not in self.running_jobs:
                should_run = True

            if should_run:
                self.start_job(job)

    def check_trigger_folder(self):
        if not TRIGGER_DIR.exists():
            return

        for trigger_folder in TRIGGER_DIR.iterdir():
            if not trigger_folder.is_dir():
                continue

            for trigger_file in trigger_folder.glob("*.txt"):
                try:
                    trigger_data = json.loads(trigger_file.read_text())
                    job = next((j for j in SCHEDULED_JOBS if j["name"] == trigger_data.get("job")), None)

                    if job and trigger_data.get("time", "immediate") == "immediate":
                        self.start_job(job, *trigger_data.get("args", []), **trigger_data.get("kwargs", {}))
                except Exception as e:
                    logger.error(f"Error processing trigger {trigger_file}: {e}")

            processed_path = TRIGGER_PROCESSED_DIR / f"{trigger_folder.name}_{int(time.time())}"
            __import__('shutil').move(str(trigger_folder), str(processed_path))

    def run(self):
        logger.info(f"Orchestrator started on device {DEVICE_ID} with tags {DEVICE_TAGS}")

        while not self.stop_event.is_set():
            try:
                self.check_scheduled_jobs()
                self.check_trigger_folder()
            except Exception as e:
                logger.error(f"Scheduler error: {e}")

            time.sleep(1)

    def stop(self):
        self.stop_event.set()
        self.executor.shutdown(wait=True)

def main():
    scheduler = JobScheduler()
    try:
        scheduler.run()
    except KeyboardInterrupt:
        logger.info("Shutting down orchestrator")
        scheduler.stop()

if __name__ == "__main__":
    main()