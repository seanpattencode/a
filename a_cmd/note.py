"""aio note - Simple note management using folder sync"""
import sys
from .sync import note_add, note_ack, note_list, pull, rebuild

def run():
    pull(); raw = ' '.join(sys.argv[2:]) if len(sys.argv) > 2 else None
    if raw and raw[0]!='?': note_add(raw); print("✓"); return
    notes = note_list()
    if not notes: print("a n <text>"); return
    if not sys.stdin.isatty(): [print(n[1]) for n in notes[:10]]; return
    print(f"{len(notes)} notes | [a]ck [s]earch [q]uit"); i = 0
    while i < len(notes):
        nid, txt, ts = notes[i]
        print(f"\n[{i+1}/{len(notes)}] {txt}"); ch = input("> ").strip().lower()
        if ch == 'a': note_ack(nid); print("✓")
        elif ch == 's': q = input("search: "); notes = [(n[0],n[1],n[2]) for n in note_list() if q.lower() in n[1].lower()]; i=0; print(f"{len(notes)} results"); continue
        elif ch == 'q': return
        elif ch: note_add(ch); notes = note_list(); print(f"✓ [{len(notes)}]"); continue
        i += 1
