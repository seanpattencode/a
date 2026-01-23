#!/usr/bin/env python3
import subprocess
result = subprocess.run(['claude', '-p', 'List files in current directory'], capture_output=True, text=True)
print(result.stdout or result.stderr)
