# Quick Start Guide

## Install

```bash
pip install prompt_toolkit libtmux
```

## Run

```bash
python python.py
```

## Try It

Once the UI loads, type these commands:

```
❯ demo: Create file | echo 'Hello World' > test.txt
❯ demo: Show file | cat test.txt
❯ run demo
```

Watch the status update above while your input stays stable!

## View Output

```bash
ls jobs/
cd jobs/demo-*
cat test.txt
```

## Exit

```
❯ quit
```

Or press `Ctrl+C`

## Load Tasks from JSON

```bash
python python.py test_task.json
```

See `README.md` for complete documentation.
