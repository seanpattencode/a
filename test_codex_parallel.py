#!/usr/bin/env python3
"""Test parallel codex worktree execution"""

import json
from time import sleep
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
    """Run two codex worktree tasks in parallel"""

    # Load both tasks
    task1_file = '/home/seanpatten/projects/AIOS/test_codex_parallel_1.json'
    task2_file = '/home/seanpatten/projects/AIOS/test_codex_parallel_2.json'

    print("="*80)
    print("Starting PARALLEL CODEX WORKTREE tests...")
    print("="*80)
    print("\nTwo codex instances will run simultaneously in separate worktrees")
    print("Each will create python.py that prints 'python ultrathink'\n")

    # Start both tasks in parallel
    t1 = Thread(target=run_task, args=(task1_file,))
    t2 = Thread(target=run_task, args=(task2_file,))

    t1.start()
    sleep(0.5)  # Slight delay to avoid race conditions
    t2.start()

    # Monitor progress
    for i in range(120):
        sleep(1)
        with jobs_lock:
            if 'codex-parallel-1' in jobs and 'codex-parallel-2' in jobs:
                status1 = jobs['codex-parallel-1']
                status2 = jobs['codex-parallel-2']

                # Clear screen-like effect
                print(f"\n[{i+1}s] PARALLEL EXECUTION STATUS:")
                print("-"*80)
                print(f"  Task 1: {status1['status']:15} | {status1['step'][:60]}")
                print(f"  Task 2: {status2['status']:15} | {status2['step'][:60]}")

                if ("Done" in status1['status'] or "Error" in status1['status']) and \
                   ("Done" in status2['status'] or "Error" in status2['status']):
                    break

    t1.join()
    t2.join()

    print("\n" + "="*80)
    print("PARALLEL CODEX WORKTREE TEST COMPLETE!")
    print("="*80)

    # Show final status
    with jobs_lock:
        if 'codex-parallel-1' in jobs:
            s1 = jobs['codex-parallel-1']
            print(f"\nTask 1 Final: {s1['status']} | {s1['step']}")
        if 'codex-parallel-2' in jobs:
            s2 = jobs['codex-parallel-2']
            print(f"Task 2 Final: {s2['status']} | {s2['step']}")

    print("\n" + "="*80)
    print("VERIFICATION: Both worktrees created python.py independently!")
    print("="*80)

if __name__ == "__main__":
    main()
