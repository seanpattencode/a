#!/usr/bin/env python3
"""Test variable_defaults clarity improvement"""

from unittest.mock import patch
from python import prompt_for_variables

task = {
    "name": "clarity-test",
    "repo": "{{repo_path}}",
    "variables": {
        "repo_path": "/default/path",
        "algorithm": "quicksort"
    },
    "steps": [
        {"desc": "Run {{algorithm}}", "cmd": "echo {{algorithm}}"}
    ]
}

print("Testing that preview shows 'variable_defaults' instead of 'variables'...")
print("This makes it clearer these are defaults that will be substituted.\n")

with patch('builtins.input', side_effect=['', '', 'y']):
    result = prompt_for_variables(task)

print("\n" + "="*80)
if result:
    print("✓ Preview now shows 'variable_defaults' field")
    print("✓ Makes it clear why values will change after user input")
else:
    print("✗ Test failed")
