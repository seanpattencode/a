#!/usr/bin/env python3
"""Standalone test for worktree functionality"""

import libtmux
import json
from time import sleep
from pathlib import Path

# Import from python.py
import sys
sys.path.insert(0, '/home/seanpatten/projects/AIOS')
from python import execute_task, get_session, jobs, jobs_lock, server

def test_worktree():
    """Test worktree creation and execution"""

    # Load test task
    with open('/home/seanpatten/projects/AIOS/test_worktree.json') as f:
        task = json.load(f)

    print(f"Testing task: {task['name']}")
    print(f"Repo: {task['repo']}")
    print(f"Branch: {task['branch']}")
    print(f"Steps: {len(task['steps'])}")
    print()

    # Execute task
    print("Executing task...")
    execute_task(task)

    # Wait for completion
    for i in range(60):
        sleep(1)
        with jobs_lock:
            if task['name'] in jobs:
                status = jobs[task['name']]
                print(f"[{i+1}s] Status: {status['status']} | Step: {status['step']}")
                if "Done" in status['status'] or "Error" in status['status']:
                    break
            else:
                print(f"[{i+1}s] No status yet...")

    print("\nTest complete!")

    # Show final status
    with jobs_lock:
        if task['name'] in jobs:
            final = jobs[task['name']]
            print(f"\nFinal Status: {final['status']}")
            print(f"Final Step: {final['step']}")

if __name__ == "__main__":
    test_worktree()
