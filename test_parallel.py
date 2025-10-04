#!/usr/bin/env python3
"""Test parallel worktree execution"""

import libtmux
import json
from time import sleep
from pathlib import Path
from threading import Thread

# Import from python.py
import sys
sys.path.insert(0, '/home/seanpatten/projects/AIOS')
from python import execute_task, jobs, jobs_lock

def run_task(task_file):
    """Run a task from JSON file"""
    with open(task_file) as f:
        task = json.load(f)
    execute_task(task)

def main():
    """Run two worktree tasks in parallel"""

    # Load both tasks
    task1_file = '/home/seanpatten/projects/AIOS/demo_parallel_worktrees.json'
    task2_file = '/home/seanpatten/projects/AIOS/demo_parallel_worktrees2.json'

    print("Starting parallel worktree tests...")
    print()

    # Start both tasks in parallel
    t1 = Thread(target=run_task, args=(task1_file,))
    t2 = Thread(target=run_task, args=(task2_file,))

    t1.start()
    sleep(0.5)  # Slight delay to avoid race conditions
    t2.start()

    # Monitor progress
    for i in range(60):
        sleep(1)
        with jobs_lock:
            if 'parallel-test-1' in jobs and 'parallel-test-2' in jobs:
                status1 = jobs['parallel-test-1']
                status2 = jobs['parallel-test-2']
                print(f"[{i+1}s]")
                print(f"  Test 1: {status1['status']:15} | {status1['step']}")
                print(f"  Test 2: {status2['status']:15} | {status2['step']}")

                if ("Done" in status1['status'] or "Error" in status1['status']) and \
                   ("Done" in status2['status'] or "Error" in status2['status']):
                    break

    t1.join()
    t2.join()

    print("\n" + "="*80)
    print("Parallel test complete!")
    print("="*80)

    # Show final status
    with jobs_lock:
        if 'parallel-test-1' in jobs:
            s1 = jobs['parallel-test-1']
            print(f"\nTest 1 Final: {s1['status']} | {s1['step']}")
        if 'parallel-test-2' in jobs:
            s2 = jobs['parallel-test-2']
            print(f"Test 2 Final: {s2['status']} | {s2['step']}")

if __name__ == "__main__":
    main()
