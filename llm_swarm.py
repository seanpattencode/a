#!/usr/bin/env python3
import argparse
import json
from openai import OpenAI
import anthropic
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
cache_file = aios_dir / "llm_cache.json"
config_file = aios_dir / "llm_config.json"

cache_file.touch(exist_ok=True)
cache = json.loads(cache_file.read_text()) if cache_file.read_text() else {}
config = json.loads(config_file.read_text()) if config_file.exists() else {"openai_key": "", "anthropic_key": ""}

client = OpenAI(api_key=config.get("openai_key", ""))
claude = anthropic.Anthropic(api_key=config.get("anthropic_key", ""))

def query_openai(question):
    response = client.chat.completions.create(
        model="gpt-3.5-turbo",
        messages=[{"role": "user", "content": question}],
        max_tokens=200
    )
    return "GPT", response.choices[0].message.content

def query_claude(question):
    response = claude.messages.create(
        model="claude-3-haiku-20240307",
        max_tokens=200,
        messages=[{"role": "user", "content": question}]
    )
    return "Claude", response.content[0].text

def ask_llms(question):
    cache_key = question[:50]

    if cache_key in cache:
        print("Using cached response")
        responses = cache[cache_key]
    else:
        with ThreadPoolExecutor(max_workers=2) as executor:
            futures = [
                executor.submit(query_openai, question),
                executor.submit(query_claude, question)
            ]
            responses = {}
            for future in futures:
                model, answer = future.result()
                responses[model] = answer

        cache[cache_key] = responses
        cache_file.write_text(json.dumps(cache, indent=2))

    for model, answer in responses.items():
        print(f"\n=== {model} ===")
        print(answer)

    all_answers = list(responses.values())
    consensus = all_answers[0] == all_answers[1] if len(all_answers) > 1 else False
    print(f"\nConsensus: {'Yes' if consensus else 'No'}")

def list_cache():
    for question in cache.keys():
        print(question)

def clear_cache():
    cache.clear()
    cache_file.write_text("{}")
    print("Cache cleared")

def stats_command():
    total = len(cache)
    models = set()
    for entry in cache.values():
        models.update(entry.keys())
    print(f"Cached queries: {total}")
    print(f"Models used: {', '.join(models)}")

parser = argparse.ArgumentParser(description="AIOS LLM Swarm")
subparsers = parser.add_subparsers(dest="command", help="Commands")

ask_parser = subparsers.add_parser("ask", help="Ask LLMs")
ask_parser.add_argument("question", nargs="+", help="Question text")
list_parser = subparsers.add_parser("list", help="List cached queries")
clear_parser = subparsers.add_parser("clear", help="Clear cache")
stats_parser = subparsers.add_parser("stats", help="Show statistics")

args = parser.parse_args()

commands = {
    "ask": lambda: ask_llms(" ".join(args.question)),
    "list": list_cache,
    "clear": clear_cache,
    "stats": stats_command
}

command_func = commands.get(args.command, stats_command)
command_func()