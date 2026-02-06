"""Suggest a new task based on existing ones"""
import os
def run():
    print("Thinking...", flush=True)
    os.system('(cat ~/projects/adata/git/task_context.txt 2>/dev/null; echo; a task l) | gemini -p "Suggest ONE new high-impact task: [task]: [why]"')
