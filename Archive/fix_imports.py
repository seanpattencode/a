with open('aio.py', 'r') as f:
    content = f.read()

if 'import io' not in content:
    content = content.replace('import atexit', 'import atexit\nimport io')
    
with open('aio.py', 'w') as f:
    f.write(content)
print("Fixed imports")
