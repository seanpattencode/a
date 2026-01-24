"""aio diff - Show git changes"""
import sys, os, subprocess as sp, re

def run():
    sp.run(['git', 'fetch', 'origin'], capture_output=True); cwd = os.getcwd()
    b = sp.run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], capture_output=True, text=True).stdout.strip()
    target = 'origin/main' if b.startswith('wt-') else f'origin/{b}'
    committed = sp.run(['git', 'diff', f'{target}..HEAD'], capture_output=True, text=True).stdout
    uncommitted = sp.run(['git', 'diff', 'HEAD', '--diff-filter=d'], capture_output=True, text=True).stdout
    diff = committed + uncommitted
    untracked = sp.run(['git', 'ls-files', '--others', '--exclude-standard'], capture_output=True, text=True).stdout.strip()
    print(f"{cwd}\n{b} -> {target}")
    if not diff and not untracked: print("No changes"); sys.exit(0)
    G, R, X, f = '\033[48;2;26;84;42m', '\033[48;2;117;34;27m', '\033[0m', ''
    for L in diff.split('\n'):
        if L.startswith('diff --git'): f = L.split(' b/')[-1]
        elif L.startswith('@@'): m = re.search(r'\+(\d+)', L); print(f"\n{f} line {m.group(1)}:" if m else "")
        elif L.startswith('+') and not L.startswith('+++'): print(f"  {G}+ {L[1:]}{X}")
        elif L.startswith('-') and not L.startswith('---'): print(f"  {R}- {L[1:]}{X}")
    if untracked: print(f"\nUntracked:\n" + '\n'.join(f"  {G}+ {u}{X}" for u in untracked.split('\n')))
    ins = len([L for L in diff.split('\n') if L.startswith('+') and not L.startswith('+++')])
    dels = len([L for L in diff.split('\n') if L.startswith('-') and not L.startswith('---')])
    nfiles = len(set(re.findall(r'diff --git a/\S+ b/(\S+)', diff)))
    added = '\n'.join(L[1:] for L in diff.split('\n') if L.startswith('+') and not L.startswith('+++'))
    removed = '\n'.join(L[1:] for L in diff.split('\n') if L.startswith('-') and not L.startswith('---'))
    try: enc = __import__('tiktoken').get_encoding('cl100k_base').encode; ta, tr = len(enc(added)), len(enc(removed))
    except: ta, tr = len(added) // 4, len(removed) // 4
    diff and print(f"\n{nfiles} files, +{ins}/-{dels} lines | Net: {ins-dels:+} lines, {ta-tr:+} tokens")
