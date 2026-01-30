"""Test a task add"""
from pathlib import Path; import tempfile, os
f = Path(tempfile.mkdtemp()) / 'tasks.txt'
f.write_text(''); open(f, 'a').write('build screenshot bot\n'); assert 'screenshot' in f.read_text(); print('ok')
