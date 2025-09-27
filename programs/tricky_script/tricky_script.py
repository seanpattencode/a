#!/usr/bin/env python3
"""
AIOS Resistance Testing Script

This script is designed to test the robustness of the AIOS death-by-default system.
It contains various malicious patterns that should be killed by the runner timeout:
- Infinite loops
- Process spawning
- Resource exhaustion
- Fork bombs (lite version)

The AIOS runner should kill this script within the configured timeout (default 0.1s).
This ensures that no runaway processes can harm system performance.

DO NOT RUN THIS SCRIPT DIRECTLY - Only run through aios_runner.py
"""

import subprocess
import time
import multiprocessing
import os

def spawn_zombie():
    subprocess.Popen(["sleep", "100"])
    time.sleep(100)

def infinite_loop():
    while True:
        time.sleep(0.1)

def fork_bomb_lite():
    subprocess.Popen(["python3", "-c", "import time; time.sleep(100)"])
    subprocess.Popen(["python3", "-c", "import time; time.sleep(100)"])
    time.sleep(100)

def resource_hog():
    data = []
    while True:
        data.append("x" * 1000000)
        time.sleep(0.5)

def multiprocess_spawn():
    proc = multiprocessing.Process(target=infinite_loop)
    proc.start()
    time.sleep(100)

print(f"TrickyScript PID: {os.getpid()}")
print("Starting infinite operations...")
infinite_loop()