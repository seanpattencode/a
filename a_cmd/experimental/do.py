"""Accomplish as much as possible in one paragraph"""
import subprocess as sp
def run():print("Working...",flush=True);sp.run('(cat ~/projects/a-sync/task_context.txt;echo;a task l)|gemini -p "Output only. Produce real deliverables (code, text, docs) that accomplish tasks. No file access - just output the content." 2>/dev/null',shell=True)
