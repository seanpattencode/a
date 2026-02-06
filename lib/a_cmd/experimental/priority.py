"""Recommend which task should be #1"""
import subprocess as sp, os
def run():
    print("Analyzing tasks...", flush=True)
    os.system('(cat ~/projects/a-sync/task_context.txt 2>/dev/null; echo; a task l) | gemini -p "Most important task? Number + 1 sentence why."')
