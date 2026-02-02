"""One-sentence plan for each task"""
import subprocess as sp
def run():print("Planning...",flush=True);sp.run('(cat ~/projects/a-sync/task_context.txt;echo;a task l)|gemini -p "For each task: #. [one sentence plan]" 2>/dev/null',shell=True)
