#!/usr/bin/env python3
"""
AIOS Orchestrator - Automated Intelligence Operating System
Improved version with clean process management and restart capability
Consolidated with job management utilities and function runner
"""

import datetime
import importlib.util
import json
import os
import random
import shutil
import signal
import sqlite3
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT_DIR = Path(__file__).parent.resolve()
PROGRAMS_DIR = ROOT_DIR / "Programs"
STATE_DB = ROOT_DIR / "orchestrator.db"

DEVICE_ID = os.environ.get("DEVICE_ID", str(os.getpid()))
DEVICE_TAGS = {tag for tag in os.environ.get("DEVICE_TAGS", "").split(",") if tag}

PROGRAMS_DIR.mkdir(parents=True, exist_ok=True)

SCHEMA = (
    "CREATE TABLE IF NOT EXISTS jobs (job_name TEXT PRIMARY KEY, status TEXT NOT NULL, device TEXT NOT NULL, last_update REAL NOT NULL, pid INTEGER);"
    "CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp REAL NOT NULL, level TEXT NOT NULL, message TEXT NOT NULL, device TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS triggers (id INTEGER PRIMARY KEY AUTOINCREMENT, job_name TEXT NOT NULL, args TEXT NOT NULL, kwargs TEXT NOT NULL, created REAL NOT NULL, processed REAL);"
    "CREATE TABLE IF NOT EXISTS scheduled_jobs (name TEXT PRIMARY KEY, file TEXT NOT NULL, function TEXT NOT NULL, type TEXT NOT NULL, tags TEXT, retries INTEGER DEFAULT 3, time TEXT, after_time TEXT, before_time TEXT, interval_minutes INTEGER, priority INTEGER DEFAULT 0, enabled INTEGER DEFAULT 1);"
    "CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated REAL NOT NULL);"
)

DEFAULT_JOBS = [
    dict(name="web_server_daemon", file="web_server.py", function="run_server", type="always", tags=["browser"], retries=999),
    dict(name="stock_monitor", file="stock_monitor.py", function="monitor_stocks", type="always", tags=["gpu"], retries=999),
    dict(name="morning_report", file="reports.py", function="generate_morning_report", type="daily", time="09:00"),
    dict(name="random_check", file="health_check.py", function="random_health_check", type="random_daily", after_time="14:00", before_time="18:00"),
    dict(name="google_drive_backup", file="google_drive_backup.py", function="backup_to_drive", type="interval", interval_minutes=120, tags=["storage"], retries=3),
    dict(name="llm_processor", file="llm_tasks.py", function="process_llm_queue", type="trigger", tags=["gpu"]),
    dict(name="idle_baseline", file="idle_task.py", function="run_idle", type="idle", priority=-1),
]

LOCK = threading.Lock()
CONN = sqlite3.connect(STATE_DB, check_same_thread=False)
CONN.row_factory = sqlite3.Row
EXECUTOR = ThreadPoolExecutor(max_workers=10)
STOP_EVENT = threading.Event()


class ProcessManager:
    """Manages subprocess spawning with proper cleanup and restart capability"""

    def __init__(self):
        self.processes = {}  # job_name -> subprocess.Popen
        self.lock = threading.Lock()

        # Set up signal handler for child process cleanup
        signal.signal(signal.SIGCHLD, self._sigchld_handler)

    def _sigchld_handler(self, signum, frame):
        """Reap zombie children immediately when they die"""
        while True:
            try:
                pid, status = os.waitpid(-1, os.WNOHANG)
                if pid == 0:
                    break
                # Find and remove the process from our tracking
                with self.lock:
                    for job_name, proc in list(self.processes.items()):
                        if proc.pid == pid:
                            del self.processes[job_name]
                            log("DEBUG", f"Reaped zombie process {pid} for job {job_name}")
                            break
            except ChildProcessError:
                break

    def start_process(self, job_name, command):
        """Start a process in its own group for clean killing"""
        with self.lock:
            # Stop existing process if running
            if job_name in self.processes:
                self.stop_process(job_name)

            try:
                # Start new process group for clean termination
                if os.name != 'nt':  # Unix/Linux
                    proc = subprocess.Popen(
                        command,
                        shell=True,
                        preexec_fn=os.setsid,  # Create new process group
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE
                    )
                else:  # Windows
                    proc = subprocess.Popen(
                        command,
                        shell=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
                    )

                self.processes[job_name] = proc
                log("INFO", f"Started process {proc.pid} for job {job_name}")
                return proc.pid

            except Exception as e:
                log("ERROR", f"Failed to start process for {job_name}: {e}")
                return None

    def stop_process(self, job_name):
        """Kill process and ALL its children"""
        with self.lock:
            if job_name not in self.processes:
                return

            proc = self.processes[job_name]
            try:
                if os.name != 'nt':  # Unix/Linux
                    # Kill entire process group
                    os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                        proc.wait(timeout=2)
                else:  # Windows
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=2)

                log("INFO", f"Stopped process {proc.pid} for job {job_name}")

            except Exception as e:
                log("ERROR", f"Error stopping process for {job_name}: {e}")
            finally:
                if job_name in self.processes:
                    del self.processes[job_name]

    def stop_all(self):
        """Clean shutdown of all processes"""
        job_names = list(self.processes.keys())
        for job_name in job_names:
            self.stop_process(job_name)
        log("INFO", f"Stopped all {len(job_names)} processes")

    def is_running(self, job_name):
        """Check if a job's process is running"""
        with self.lock:
            if job_name not in self.processes:
                return False
            proc = self.processes[job_name]
            return proc.poll() is None

    def get_process_info(self):
        """Get info about all running processes"""
        with self.lock:
            info = {}
            for job_name, proc in self.processes.items():
                info[job_name] = {
                    'pid': proc.pid,
                    'running': proc.poll() is None
                }
            return info


# Global process manager instance
PROCESS_MANAGER = ProcessManager()


def db_execute(sql, params=(), fetch=None):
    with LOCK:
        cursor = CONN.execute(sql, params)
        CONN.commit()
        if fetch == "one":
            return cursor.fetchone()
        if fetch == "all":
            return cursor.fetchall()
        return None


def log(level, message):
    timestamp = time.time()
    db_execute(
        "INSERT INTO logs (timestamp, level, message, device) VALUES (?, ?, ?, ?)",
        (timestamp, level, message, DEVICE_ID)
    )
    now = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')
    print(f"{now} [{level}] {message}")


def update_job(job_name, status, pid=None):
    db_execute(
        (
            "INSERT INTO jobs (job_name, status, device, last_update, pid) VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(job_name) DO UPDATE SET status=excluded.status, device=excluded.device, "
            "last_update=excluded.last_update, pid=excluded.pid"
        ),
        (job_name, status, DEVICE_ID, time.time(), pid)
    )


def add_trigger(job_name, args=None, kwargs=None):
    db_execute(
        "INSERT INTO triggers (job_name, args, kwargs, created) VALUES (?, ?, ?, ?)",
        (job_name, json.dumps(args or []), json.dumps(kwargs or {}), time.time())
    )


def get_config(key):
    """Get a config value from the database"""
    result = db_execute("SELECT value FROM config WHERE key = ?", (key,), fetch="one")
    return result["value"] if result else None


def set_config(key, value):
    """Set a config value in the database"""
    db_execute(
        "INSERT OR REPLACE INTO config (key, value, updated) VALUES (?, ?, ?)",
        (key, value, time.time())
    )


def init_db():
    with CONN:
        CONN.executescript(SCHEMA)
    if db_execute("SELECT 1 FROM scheduled_jobs LIMIT 1", fetch="one"):
        return
    for job in DEFAULT_JOBS:
        db_execute(
            (
                "INSERT OR IGNORE INTO scheduled_jobs "
                "(name, file, function, type, tags, retries, time, after_time, before_time, interval_minutes, priority, enabled) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)"
            ),
            (
                job["name"],
                job["file"],
                job["function"],
                job["type"],
                json.dumps(job.get("tags", [])),
                job.get("retries", 3),
                job.get("time"),
                job.get("after_time"),
                job.get("before_time"),
                job.get("interval_minutes"),
                job.get("priority", 0),
            )
        )
    log("INFO", "Populated default scheduled jobs")


def _minutes(value):
    hours, minutes = map(int, value.split(":"))
    return hours * 60 + minutes


def job_due(job, now_minutes, now_ts, today, non_idle_running):
    job_type = job["type"]
    if job_type == "trigger" or (job["tags"] and not job["tag_set"].issubset(DEVICE_TAGS)):
        return False
    status = job.get("status", "")
    if status == "running":
        return False
    if job_type == "always":
        return True
    if job_type == "idle":
        return not non_idle_running

    last_run = job.get("last_update")
    ran_today = bool(last_run) and datetime.datetime.fromtimestamp(last_run).date() == today
    if job_type == "daily":
        target = job.get("time")
        return bool(target) and now_minutes >= _minutes(target) and not ran_today
    if job_type == "random_daily":
        if ran_today:
            return False
        start = _minutes(job.get("after_time", "00:00"))
        end = _minutes(job.get("before_time", "23:59"))
        return start <= now_minutes <= end and random.random() < 0.01
    if job_type == "interval":
        interval = job.get("interval_minutes")
        return bool(interval) and (not last_run or (now_ts - last_run) / 60 >= interval)
    return False


def load_and_call_function(file_path, function_name, *args, **kwargs):
    script_path = PROGRAMS_DIR / file_path
    if not script_path.exists():
        script_path.write_text(
            f"\ndef {function_name}(*args, **kwargs):\n    print(\"{function_name} called\")\n    return \"{function_name} completed\"\n"
        )

    spec = importlib.util.spec_from_file_location("module", script_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    func = getattr(module, function_name, None)
    return func(*args, **kwargs) if func else None


def execute_job(job, args, kwargs):
    """Execute a job, using ProcessManager for 'always' type jobs"""
    name = job["name"]
    retries = job.get("retries", 3)
    log("INFO", f"Starting job: {name}")

    # For daemon-style jobs, use ProcessManager
    if job["type"] == "always":
        script_path = PROGRAMS_DIR / job["file"]
        # Use orchestrator.py itself with --run-function flag instead of separate run_function.py
        command = f"{sys.executable} {__file__} --run-function {script_path} {job['function']}"

        for attempt in range(retries):
            pid = PROCESS_MANAGER.start_process(name, command)
            if pid:
                update_job(name, "running", pid)
                return
            if attempt < retries - 1:
                time.sleep(2 ** attempt)

        update_job(name, "failed")
        return

    # For regular jobs, use the existing import method
    for attempt in range(retries):
        update_job(name, "running")
        try:
            result = load_and_call_function(job["file"], job["function"], *args, **kwargs)
        except Exception as exc:
            log("ERROR", f"Job {name} failed: {exc}")
            if attempt < retries - 1:
                time.sleep(2 ** attempt)
        else:
            log("INFO", f"Job {name} completed with result: {result}")
            update_job(name, "completed")
            return
    update_job(name, "failed")


def start_job(job, *args, **kwargs):
    update_job(job["name"], "running")
    EXECUTOR.submit(execute_job, job, args, kwargs)


def restart_job(job_name):
    """Cleanly restart a specific job"""
    log("INFO", f"Restarting job: {job_name}")

    # Stop if running
    PROCESS_MANAGER.stop_process(job_name)

    # Clear status
    update_job(job_name, "stopped")

    # Get job config
    job_row = db_execute(
        "SELECT * FROM scheduled_jobs WHERE name = ?",
        (job_name,),
        fetch="one"
    )

    if job_row:
        job = dict(job_row)
        job["tags"] = json.loads(job["tags"]) if job["tags"] else []
        job["tag_set"] = set(job["tags"])
        start_job(job)
        log("INFO", f"Restarted {job_name}")
    else:
        log("ERROR", f"Job {job_name} not found")


def restart_all():
    """Clean restart of entire system"""
    log("INFO", "Restarting all jobs...")

    # Stop all processes cleanly
    PROCESS_MANAGER.stop_all()

    # Reset all job statuses
    db_execute("UPDATE jobs SET status = 'stopped', pid = NULL")

    log("INFO", "All jobs stopped, scheduler will restart them as needed")


def run():
    log("INFO", f"Orchestrator started on device {DEVICE_ID} with tags {sorted(DEVICE_TAGS)}")

    # Clean up any stale process entries on startup
    db_execute("UPDATE jobs SET pid = NULL WHERE status != 'running'")

    while not STOP_EVENT.is_set():
        try:
            rows = db_execute(
                "SELECT s.*, j.status, j.last_update FROM scheduled_jobs s "
                "LEFT JOIN jobs j ON s.name = j.job_name WHERE s.enabled = 1",
                fetch="all"
            ) or []
            jobs = [
                {
                    **dict(row),
                    "tags": tags,
                    "tag_set": set(tags),
                    "status": row["status"] or "",
                    "last_update": row["last_update"],
                }
                for row in rows
                for tags in [json.loads(row["tags"]) if row["tags"] else []]
            ]
            non_idle_running = any(
                job["status"] == "running" and job["type"] != "idle" for job in jobs
            )
            now = datetime.datetime.now()
            now_minutes = now.hour * 60 + now.minute
            today = now.date()
            now_ts = time.time()

            for job in jobs:
                # Check if process-managed jobs are still running
                if job["type"] == "always" and job["status"] == "running":
                    if not PROCESS_MANAGER.is_running(job["name"]):
                        log("WARNING", f"Job {job['name']} died, restarting")
                        update_job(job["name"], "stopped")
                        job["status"] = "stopped"

                if job_due(job, now_minutes, now_ts, today, non_idle_running):
                    start_job(job)

            jobs_by_name = {job["name"]: job for job in jobs}
            triggers = db_execute(
                "SELECT id, job_name, args, kwargs FROM triggers WHERE processed IS NULL",
                fetch="all"
            ) or []
            for trigger in triggers:
                # Special system triggers
                if trigger["job_name"] == "SYSTEM_RESTART":
                    restart_all()
                    db_execute("UPDATE triggers SET processed = ? WHERE id = ?", (time.time(), trigger["id"]))
                    continue
                elif trigger["job_name"].startswith("RESTART_"):
                    job_to_restart = trigger["job_name"][8:]  # Remove "RESTART_" prefix
                    restart_job(job_to_restart)
                    db_execute("UPDATE triggers SET processed = ? WHERE id = ?", (time.time(), trigger["id"]))
                    continue

                # Normal job triggers
                job = jobs_by_name.get(trigger["job_name"])
                if not job or (job["tags"] and not job["tag_set"].issubset(DEVICE_TAGS)):
                    db_execute("UPDATE triggers SET processed = ? WHERE id = ?", (time.time(), trigger["id"]))
                    continue
                args = json.loads(trigger["args"])
                kwargs = json.loads(trigger["kwargs"])
                start_job(job, *args, **kwargs)
                db_execute("UPDATE triggers SET processed = ? WHERE id = ?", (time.time(), trigger["id"]))
        except Exception as exc:
            log("ERROR", f"Scheduler error: {exc}")
        time.sleep(1)


def stop():
    log("INFO", "Shutting down orchestrator...")
    PROCESS_MANAGER.stop_all()
    STOP_EVENT.set()
    EXECUTOR.shutdown(wait=True)
    log("INFO", "Orchestrator shutdown complete")


def manage_jobs_cli():
    """Job management CLI functionality (consolidated from manage_jobs.py)"""

    def list_jobs():
        """List all scheduled jobs with their configuration"""
        conn = sqlite3.connect(STATE_DB)
        conn.row_factory = sqlite3.Row
        cursor = conn.execute("SELECT * FROM scheduled_jobs ORDER BY name")

        print("\nScheduled Jobs:")
        print("-" * 80)
        for row in cursor:
            tags = json.loads(row["tags"]) if row["tags"] else []
            print(f"Name: {row['name']}")
            print(f"  Type: {row['type']}")
            print(f"  File: {row['file']}")
            print(f"  Function: {row['function']}")
            print(f"  Enabled: {'Yes' if row['enabled'] else 'No'}")
            if tags:
                print(f"  Tags: {', '.join(tags)}")
            if row["time"]:
                print(f"  Time: {row['time']}")
            if row["interval_minutes"]:
                print(f"  Interval: {row['interval_minutes']} minutes")
            if row["retries"]:
                print(f"  Retries: {row['retries']}")
            print()
        conn.close()

    def enable_job(job_name):
        """Enable a scheduled job"""
        conn = sqlite3.connect(STATE_DB)
        conn.execute("UPDATE scheduled_jobs SET enabled = 1 WHERE name = ?", (job_name,))
        conn.commit()
        conn.close()
        print(f"Job '{job_name}' enabled")

    def disable_job(job_name):
        """Disable a scheduled job"""
        conn = sqlite3.connect(STATE_DB)
        conn.execute("UPDATE scheduled_jobs SET enabled = 0 WHERE name = ?", (job_name,))
        conn.commit()
        conn.close()
        print(f"Job '{job_name}' disabled")

    def status():
        """Show current system status and job execution state"""
        conn = sqlite3.connect(STATE_DB)
        conn.row_factory = sqlite3.Row

        print("\n" + "=" * 80)
        print("AIOS System Status")
        print("=" * 80)

        # Count jobs by status
        cursor = conn.execute("""
            SELECT
                (SELECT COUNT(*) FROM scheduled_jobs WHERE enabled = 1) as enabled,
                (SELECT COUNT(*) FROM scheduled_jobs WHERE enabled = 0) as disabled,
                (SELECT COUNT(*) FROM jobs WHERE status = 'running') as running,
                (SELECT COUNT(*) FROM jobs WHERE status = 'failed') as failed,
                (SELECT COUNT(*) FROM triggers WHERE processed IS NULL) as pending_triggers
        """)
        stats = cursor.fetchone()

        print(f"\n📊 Job Statistics:")
        print(f"  Enabled Jobs: {stats['enabled']}")
        print(f"  Disabled Jobs: {stats['disabled']}")
        print(f"  Currently Running: {stats['running']}")
        print(f"  Failed Jobs: {stats['failed']}")
        print(f"  Pending Triggers: {stats['pending_triggers']}")

        # Show recent job executions
        print(f"\n📋 Recent Job Executions (last 5):")
        cursor = conn.execute("""
            SELECT job_name, status, last_update
            FROM jobs
            ORDER BY last_update DESC
            LIMIT 5
        """)
        for row in cursor:
            timestamp = datetime.datetime.fromtimestamp(row['last_update']).strftime('%Y-%m-%d %H:%M:%S')
            status_icon = "✅" if row['status'] == 'completed' else "❌" if row['status'] == 'failed' else "🔄"
            print(f"  {status_icon} {row['job_name']}: {row['status']} at {timestamp}")

        conn.close()

    def trigger_job(job_name, *args, **kwargs):
        """Trigger a job for execution"""
        conn = sqlite3.connect(STATE_DB)

        # Check if job exists
        cursor = conn.execute("SELECT type FROM scheduled_jobs WHERE name = ?", (job_name,))
        job = cursor.fetchone()
        if not job:
            print(f"Error: Job '{job_name}' not found")
            conn.close()
            return

        # Insert trigger
        conn.execute(
            "INSERT INTO triggers (job_name, args, kwargs, created) VALUES (?, ?, ?, ?)",
            (job_name, json.dumps(list(args)), json.dumps(kwargs), time.time())
        )
        conn.commit()
        print(f"Trigger added for job '{job_name}'")
        conn.close()

    # Parse command line arguments for manage_jobs functionality
    if len(sys.argv) < 2:
        return False  # Not a management command

    command = sys.argv[1]

    if command == "list":
        list_jobs()
        return True
    elif command == "enable" and len(sys.argv) >= 3:
        enable_job(sys.argv[2])
        return True
    elif command == "disable" and len(sys.argv) >= 3:
        disable_job(sys.argv[2])
        return True
    elif command == "status":
        status()
        return True
    elif command == "trigger" and len(sys.argv) >= 3:
        job_name = sys.argv[2]
        # Parse key=value arguments
        kwargs = {}
        for arg in sys.argv[3:]:
            if '=' in arg:
                key, value = arg.split('=', 1)
                kwargs[key] = value
        trigger_job(job_name, **kwargs)
        return True
    elif command == "help" or command == "--help":
        print("AIOS Orchestrator - Job Management Commands")
        print("=" * 60)
        print("\nUsage:")
        print("  python orchestrator.py [command] [args...]")
        print("\nJob Management:")
        print("  list                    - List all scheduled jobs")
        print("  enable <job_name>       - Enable a job")
        print("  disable <job_name>      - Disable a job")
        print("  trigger <job_name>      - Trigger a job")
        print("  status                  - Show system status")
        print("\nOrchestrator:")
        print("  --force                 - Start orchestrator (force restart)")
        print("  (no args)              - Start orchestrator normally")
        return True

    return False


def main():
    # Handle --run-function mode (embedded run_function.py functionality)
    if len(sys.argv) >= 4 and sys.argv[1] == "--run-function":
        module_file = Path(sys.argv[2])
        function_name = sys.argv[3]
        args = sys.argv[4:] if len(sys.argv) > 4 else []

        # Import and execute the function
        spec = importlib.util.spec_from_file_location("module", module_file)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        func = getattr(module, function_name, None)
        if func:
            func(*args)
        else:
            print(f"Function {function_name} not found in {module_file}")
            sys.exit(1)
        return

    # Check if this is a management command
    if manage_jobs_cli():
        return

    # Otherwise, run the orchestrator
    # Check for virtual environment (recommended but not required)
    if not hasattr(sys, 'real_prefix') and sys.base_prefix == sys.prefix:
        print("\n" + "="*60)
        print("Note: Running outside virtual environment")
        print("Recommended: python -m venv .venv && source .venv/bin/activate")
        print("="*60 + "\n")

    try:
        run()
    except KeyboardInterrupt:
        log("INFO", "Received interrupt signal")
        stop()


# Initialize database on module load
init_db()

if __name__ == "__main__":
    main()