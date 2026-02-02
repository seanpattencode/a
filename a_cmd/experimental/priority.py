"""Recommend which task should be #1"""
import subprocess as sp
def run():print("Thinking...",flush=True);sp.run('(cat ~/projects/a-sync/task_context.txt;echo;a task l)|gemini -p "Most important task? Number + 1 sentence why." 2>/dev/null',shell=True)
