"""Suggest a new task based on existing ones"""
import subprocess as sp
def run():print("Thinking...",flush=True);sp.run('(cat ~/projects/a-sync/task_context.txt;echo;a task l)|gemini -p "Suggest ONE new high-impact task: [task]: [why]" 2>/dev/null',shell=True)
