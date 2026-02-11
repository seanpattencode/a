"""Accomplish as much as possible in one paragraph"""
import os
def run():
    print("Working...", flush=True)
    os.system('(cat ~/projects/adata/git/task_context.txt 2>/dev/null; echo; a task l) | gemini -p "Output only. Produce real deliverables (code, text, docs) that accomplish tasks. No file access - just output the content."')
