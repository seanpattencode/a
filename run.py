#!/usr/bin/env python3
"""Simple task runner with menu - no UI"""

import sys
from pathlib import Path
import json
from time import sleep
from threading import Thread
from python import execute_task, jobs, jobs_lock, show_task_menu

def monitor_jobs(timeout=60):
    """Monitor job execution"""
    for i in range(timeout):
        sleep(1)
        with jobs_lock:
            if not jobs:
                continue

            all_done = all(
                "Done" in j['status'] or "Error" in j['status']
                for j in jobs.values()
            )

            if i % 2 == 0:
                statuses = [f"{name}: {info['status']}" for name, info in jobs.items()]
                print(f"[{i}s] {' | '.join(statuses)}")

            if all_done:
                break

    print("\n" + "="*80)
    with jobs_lock:
        for name, info in jobs.items():
            print(f"{name}: {info['status']} | {info['step']}")
    print("="*80)

def main():
    print("AIOS Simple Runner")

    # Show menu and get selections
    selected_tasks = show_task_menu()

    if not selected_tasks:
        print("No tasks selected")
        return

    print(f"\nStarting {len(selected_tasks)} task(s)...")

    # Run tasks
    threads = []
    for task in selected_tasks:
        t = Thread(target=execute_task, args=(task,))
        threads.append(t)
        t.start()
        sleep(0.5)

    # Monitor
    monitor_jobs()

    # Wait for completion
    for t in threads:
        t.join()

    print("\n✓ All tasks complete!")

if __name__ == "__main__":
    main()
