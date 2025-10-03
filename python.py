import libtmux
from datetime import datetime
from time import sleep

server = libtmux.Server()
get_session = lambda n: next((s for s in server.sessions if s.name == n), None)
run = lambda n, c: server.new_session(n, window_command=c, attach=False)
send = lambda n, c: get_session(n).windows[0].panes[0].send_keys(c)
capture = lambda n: get_session(n).windows[0].panes[0].capture_pane()
kill = lambda n: get_session(n).kill() if get_session(n) else None
wait_ready = lambda n: next((x for x in iter(lambda: sleep(1) or ("bash-5.2$" in "\n".join(capture(n)[-5:])), True)), None)

def demo():
    print("Starting parallel demo")
    d1, d2 = datetime.now().strftime("%Y%m%d_%H%M%S_1"), datetime.now().strftime("%Y%m%d_%H%M%S_2")
    kill("aios-demo-1"), kill("aios-demo-2")
    run("aios-demo-1", "bash --norc --noprofile"), run("aios-demo-2", "bash --norc --noprofile")
    send("aios-demo-1", f"mkdir -p {d1} && cd {d1}"), send("aios-demo-2", f"mkdir -p {d2} && cd {d2}")
    sleep(1)
    send("aios-demo-1", 'codex exec -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox -- "create a python program that factors prime numbers in 10 lines or less, save it to factor.py"')
    send("aios-demo-2", 'codex exec -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox -- "create a python program that factors prime numbers in 10 lines or less, save it to factor.py"')
    print("Waiting for step 1 to complete...")
    wait_ready("aios-demo-1"), wait_ready("aios-demo-2")
    send("aios-demo-1", "echo 'Input: 84' > output.txt && echo 84 | python factor.py >> output.txt")
    send("aios-demo-2", "echo 'Input: 84' > output.txt && python factor.py 84 >> output.txt")
    sleep(2)
    send("aios-demo-1", 'codex exec -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox -- "refactor factor.py to use only direct library calls and no other way, save to factor2.py. Then create test.py that imports both factor.py and factor2.py and runs them with input 84, showing both outputs"')
    send("aios-demo-2", 'codex exec -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox -- "refactor factor.py to use only direct library calls and no other way, save to factor2.py. Then create test.py that imports both factor.py and factor2.py and runs them with input 84, showing both outputs"')
    print("Waiting for step 2 to complete...")
    wait_ready("aios-demo-1"), wait_ready("aios-demo-2")
    send("aios-demo-1", "echo '=== Comparison Test ===' > output2.txt && python test.py >> output2.txt")
    send("aios-demo-2", "echo '=== Comparison Test ===' > output2.txt && python test.py >> output2.txt")
    sleep(2)
    print(f"\naios-demo-1:\n{'\n'.join(capture('aios-demo-1'))}")
    print(f"\naios-demo-2:\n{'\n'.join(capture('aios-demo-2'))}")

__name__ == "__main__" and demo()
