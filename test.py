#!/usr/bin/env python3
"""AIOS Consolidated Test Suite - Minimal, Fast, Configurable"""

import sys
import json
from pathlib import Path
from time import sleep
from threading import Thread
from python import execute_task, jobs, jobs_lock

# Test configurations
TESTS = {
    "basic": {
        "desc": "Basic worktree test",
        "tasks": [{
            "name": "basic-test",
            "repo": "/home/seanpatten/projects/testRepoPrivate",
            "branch": "main",
            "steps": [
                {"desc": "Show directory", "cmd": "pwd"},
                {"desc": "Show git status", "cmd": "git status --short"},
                {"desc": "Create test file", "cmd": "echo 'test' > test.txt"},
                {"desc": "Show file", "cmd": "cat test.txt"}
            ]
        }]
    },

    "codex": {
        "desc": "Codex in worktree",
        "tasks": [{
            "name": "codex-test",
            "repo": "/home/seanpatten/projects/testRepoPrivate",
            "branch": "main",
            "steps": [
                {"desc": "Create python.py via codex", "cmd": "codex exec --sandbox workspace-write -- \"create a file named python.py that prints 'python ultrathink'\""},
                {"desc": "Show file", "cmd": "cat python.py"},
                {"desc": "Run python.py", "cmd": "python python.py"},
                {"desc": "Verify", "cmd": "python python.py | grep -q 'python ultrathink' && echo 'SUCCESS'"}
            ]
        }]
    },

    "parallel": {
        "desc": "Parallel worktree execution",
        "tasks": [
            {
                "name": "parallel-1",
                "repo": "/home/seanpatten/projects/testRepoPrivate",
                "branch": "main",
                "steps": [
                    {"desc": "Show dir", "cmd": "pwd"},
                    {"desc": "Create file", "cmd": "echo 'task 1' > result.txt"},
                    {"desc": "Show result", "cmd": "cat result.txt"}
                ]
            },
            {
                "name": "parallel-2",
                "repo": "/home/seanpatten/projects/testRepoPrivate",
                "branch": "main",
                "steps": [
                    {"desc": "Show dir", "cmd": "pwd"},
                    {"desc": "Create file", "cmd": "echo 'task 2' > result.txt"},
                    {"desc": "Show result", "cmd": "cat result.txt"}
                ]
            }
        ]
    },

    "parallel-codex": {
        "desc": "Parallel codex in worktrees",
        "tasks": [
            {
                "name": "codex-1",
                "repo": "/home/seanpatten/projects/testRepoPrivate",
                "branch": "main",
                "steps": [
                    {"desc": "Create python.py", "cmd": "codex exec --sandbox workspace-write -- \"create python.py that prints 'python ultrathink'\""},
                    {"desc": "Run", "cmd": "python python.py"}
                ]
            },
            {
                "name": "codex-2",
                "repo": "/home/seanpatten/projects/testRepoPrivate",
                "branch": "main",
                "steps": [
                    {"desc": "Create python.py", "cmd": "codex exec --sandbox workspace-write -- \"create python.py that prints 'python ultrathink'\""},
                    {"desc": "Run", "cmd": "python python.py"}
                ]
            }
        ]
    },

    "no-worktree": {
        "desc": "Basic test without worktree",
        "tasks": [{
            "name": "no-worktree-test",
            "steps": [
                {"desc": "Show dir", "cmd": "pwd"},
                {"desc": "Create file", "cmd": "echo 'no worktree' > test.txt"},
                {"desc": "Show file", "cmd": "cat test.txt"}
            ]
        }]
    }
}

def run_task(task):
    """Execute a single task"""
    execute_task(task)

def monitor_tasks(task_names, timeout=120):
    """Monitor task execution until completion"""
    for i in range(timeout):
        sleep(1)
        with jobs_lock:
            all_done = True
            statuses = []
            for name in task_names:
                if name in jobs:
                    status = jobs[name]
                    statuses.append(f"{name}: {status['status']:15} | {status['step'][:50]}")
                    if "Done" not in status['status'] and "Error" not in status['status']:
                        all_done = False
                else:
                    all_done = False
                    statuses.append(f"{name}: Waiting...")

            if i % 2 == 0:  # Update every 2 seconds
                print(f"\r[{i+1}s] {' | '.join(statuses)}", end='', flush=True)

            if all_done:
                print()  # Newline
                break

    print()  # Final newline

    # Show final status
    with jobs_lock:
        print("\n" + "="*80)
        print("FINAL STATUS:")
        print("="*80)
        for name in task_names:
            if name in jobs:
                s = jobs[name]
                print(f"{name}: {s['status']} | {s['step']}")
        print("="*80)

def run_test(test_name):
    """Run a specific test configuration"""
    if test_name not in TESTS:
        print(f"Error: Test '{test_name}' not found")
        print(f"Available tests: {', '.join(TESTS.keys())}")
        return False

    test = TESTS[test_name]
    tasks = test['tasks']

    print("="*80)
    print(f"RUNNING TEST: {test_name}")
    print(f"Description: {test['desc']}")
    print(f"Tasks: {len(tasks)}")
    print("="*80)
    print()

    # Start tasks
    threads = []
    task_names = []
    for task in tasks:
        task_names.append(task['name'])
        t = Thread(target=run_task, args=(task,))
        threads.append(t)
        t.start()
        sleep(0.5)  # Slight delay between task starts

    # Monitor execution
    monitor_tasks(task_names)

    # Wait for threads
    for t in threads:
        t.join()

    print(f"\n✓ Test '{test_name}' complete!\n")
    return True

def list_tests():
    """List all available tests"""
    print("Available Tests:")
    print("="*80)
    for name, config in TESTS.items():
        print(f"  {name:20} - {config['desc']}")
    print("="*80)

def save_test_as_json(test_name, output_dir="test_configs"):
    """Save a test configuration as JSON file(s)"""
    if test_name not in TESTS:
        print(f"Error: Test '{test_name}' not found")
        return False

    output_path = Path(output_dir)
    output_path.mkdir(exist_ok=True)

    tasks = TESTS[test_name]['tasks']
    for task in tasks:
        filename = output_path / f"{task['name']}.json"
        with open(filename, 'w') as f:
            json.dump(task, f, indent=2)
        print(f"Saved: {filename}")

    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: python test.py <command> [options]")
        print()
        print("Commands:")
        print("  list              - List all available tests")
        print("  run <test_name>   - Run a specific test")
        print("  save <test_name>  - Save test as JSON file(s)")
        print("  all               - Run all tests sequentially")
        print()
        list_tests()
        return

    command = sys.argv[1]

    if command == "list":
        list_tests()

    elif command == "run":
        if len(sys.argv) < 3:
            print("Error: Please specify a test name")
            list_tests()
            return
        test_name = sys.argv[2]
        run_test(test_name)

    elif command == "save":
        if len(sys.argv) < 3:
            print("Error: Please specify a test name")
            list_tests()
            return
        test_name = sys.argv[2]
        save_test_as_json(test_name)

    elif command == "all":
        for test_name in TESTS.keys():
            run_test(test_name)
            print()

    else:
        print(f"Unknown command: {command}")
        print("Run 'python test.py' for usage")

if __name__ == "__main__":
    main()
