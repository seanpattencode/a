#!/usr/bin/env python3
import subprocess
result = subprocess.run(['claude', '-p', '--dangerously-skip-permissions', 'Check current weather in NYC'], capture_output=True, text=True)
print(result.stdout or result.stderr)
