#!/usr/bin/env python3
# experimental — simplest platonic agent: ollama model via argv, chat + CMD exec, prints model.
import subprocess as S, ollama, sys
M = sys.argv[2] if len(sys.argv) > 2 else "gemma4-crunch:latest"; m = []; print(f"model: {M}  (switch: a oa <model>  list: ollama list)")
while u := input("\n> ").strip():
    m += [{"role": "user", "content": u}]
    t = ollama.chat(M, m).message.content.strip(); print(t); m += [{"role": "assistant", "content": t}]
    if "CMD:" in t:
        c = t[t.index("CMD:")+4:].split("\n")[0].strip(" `")
        o = S.run(c, shell=1, capture_output=1, text=1, timeout=30).stdout.strip()
        print(f"${c}\n{o}"); m += [{"role": "user", "content": o}]
