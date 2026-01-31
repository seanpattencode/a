"""a ask <prompt> - query multiple LLMs in parallel"""
import sys,os,threading,queue
from pathlib import Path
from ._common import SYNC_ROOT

KEYS_FILE = SYNC_ROOT/'login'/'api_keys.env'

def load_keys():
    if not KEYS_FILE.exists(): return {}
    return dict(l.strip().split('=',1) for l in KEYS_FILE.read_text().splitlines() if '=' in l and not l.startswith('#'))

MODELS = {'claude':'claude-opus-4-5-20251101', 'gpt':'gpt-5.2', 'gemini':'gemini-3-pro-preview'}

def ask_anthropic(prompt, q, keys):
    m = MODELS['claude']
    try:
        from anthropic import Anthropic
        r = Anthropic(api_key=keys.get('ANTHROPIC_API_KEY')).messages.create(model=m, max_tokens=512, messages=[{'role':'user','content':prompt}])
        q.put((f'claude ({m})', r.content[0].text))
    except Exception as e: q.put((f'claude ({m})', f'x {e}'))

def ask_openai(prompt, q, keys):
    m = MODELS['gpt']
    try:
        from openai import OpenAI
        r = OpenAI(api_key=keys.get('OPENAI_API_KEY')).chat.completions.create(model=m, messages=[{'role':'user','content':prompt}], max_completion_tokens=512)
        q.put((f'gpt ({m})', r.choices[0].message.content))
    except Exception as e: q.put((f'gpt ({m})', f'x {e}'))

def ask_gemini(prompt, q, keys):
    m = MODELS['gemini']
    try:
        import google.generativeai as genai
        genai.configure(api_key=keys.get('GOOGLE_API_KEY'))
        q.put((f'gemini ({m})', genai.GenerativeModel(m).generate_content(prompt).text))
    except Exception as e: q.put((f'gemini ({m})', f'x {e}'))

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
