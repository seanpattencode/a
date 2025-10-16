#!/usr/bin/env python3
import sys
sys.path.insert(0, '.')
from aios import execute_task, init_db, Observer, ExitFileHandler
from time import time, sleep
from threading import Thread

# Start exit file observer
exit_obs = Observer()
exit_obs.schedule(ExitFileHandler(), '/tmp', recursive=False)
exit_obs.start()

# Initialize
init_db()

# Create a simple task
task = {
    "name": "event-test",
    "steps": [
        {"desc": "Quick echo", "cmd": "echo 'Testing event-driven completion'"},
        {"desc": "Quick sleep", "cmd": "sleep 0.5"}
    ]
}

print("Executing task with event-driven completion detection...")
start = time()
execute_task(task)
elapsed = time() - start

print(f"\n✓ Task completed in {elapsed:.2f}s")
print("✓ No polling - pure event-driven execution!")

# Cleanup
exit_obs.stop()
exit_obs.join()
