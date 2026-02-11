"""One-sentence plan for each task"""
import os
def run():
    print("Planning...", flush=True)
    os.system('(cat ~/projects/a-sync/task_context.txt 2>/dev/null; echo; a task l) | gemini -p "For each task: #. [one sentence plan]"')
