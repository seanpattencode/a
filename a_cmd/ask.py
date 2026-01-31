"""a ask <prompt> - query multiple LLMs in parallel"""
import sys,os,threading,queue
from pathlib import Path
from ._common import SYNC_ROOT

KEYS_FILE = SYNC_ROOT/'login'/'api_keys.env'

def load_keys():
    if not KEYS_FILE.exists(): return {}
    return dict(l.strip().split('=',1) for l in KEYS_FILE.read_text().splitlines() if '=' in l and not l.startswith('#'))

def ask_anthropic(prompt, q, keys):
    try:
        from anthropic import Anthropic
        c = Anthropic(api_key=keys.get('ANTHROPIC_API_KEY'))
        r = c.messages.create(model='claude-3-5-haiku-20241022', max_tokens=512, messages=[{'role':'user','content':prompt}])
        q.put(('claude', r.content[0].text))
    except Exception as e: q.put(('claude', f'x {e}'))

def ask_openai(prompt, q, keys):
    try:
        from openai import OpenAI
        c = OpenAI(api_key=keys.get('OPENAI_API_KEY'))
        r = c.chat.completions.create(model='gpt-4o-mini', messages=[{'role':'user','content':prompt}], max_tokens=512)
        q.put(('gpt', r.choices[0].message.content))
    except Exception as e: q.put(('gpt', f'x {e}'))

def ask_gemini(prompt, q, keys):
    try:
        import google.generativeai as genai
        genai.configure(api_key=keys.get('GOOGLE_API_KEY'))
        r = genai.GenerativeModel('gemini-2.0-flash').generate_content(prompt)
        q.put(('gemini', r.text))
    except Exception as e: q.put(('gemini', f'x {e}'))

def run():
    if len(sys.argv) < 3: print("usage: a ask <prompt>"); return
    prompt = ' '.join(sys.argv[2:])
    keys = load_keys()
    if not keys: print(f"No keys. Create {KEYS_FILE} with:\nANTHROPIC_API_KEY=sk-...\nOPENAI_API_KEY=sk-...\nGOOGLE_API_KEY=AIza..."); return

    q = queue.Queue()
    threads = []
    if keys.get('ANTHROPIC_API_KEY'): threads.append(threading.Thread(target=ask_anthropic, args=(prompt,q,keys)))
    if keys.get('OPENAI_API_KEY'): threads.append(threading.Thread(target=ask_openai, args=(prompt,q,keys)))
    if keys.get('GOOGLE_API_KEY'): threads.append(threading.Thread(target=ask_gemini, args=(prompt,q,keys)))

    if not threads: print("No valid keys found"); return
    for t in threads: t.start()

    results = []
    for _ in threads:
        name, text = q.get()
        results.append((name, text))
        print(f"\n{'='*40}\n{name.upper()}\n{'='*40}\n{text[:500]}{'...' if len(text)>500 else ''}")
    for t in threads: t.join()
