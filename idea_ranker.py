#!/usr/bin/env python3
import argparse
import json
from openai import OpenAI
from pathlib import Path

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
ideas_file = aios_dir / "ideas.txt"
ranked_file = aios_dir / "ranked_ideas.txt"
config_file = aios_dir / "llm_config.json"

ideas_file.touch(exist_ok=True)
config = json.loads(config_file.read_text()) if config_file.exists() else {"api_key": ""}
client = OpenAI(api_key=config.get("api_key", ""))

def rank_ideas():
    ideas = ideas_file.read_text().splitlines()

    if not ideas:
        print("No ideas to rank")
        return

    ideas_text = "\n".join(f"{i+1}. {idea}" for i, idea in enumerate(ideas))
    prompt = f"""Score these ideas by feasibility (1-10) and impact (1-10).
    Return as JSON: [{{"idea": "...", "feasibility": N, "impact": N, "score": N}}]
    Ideas:\n{ideas_text}"""

    response = client.chat.completions.create(
        model="gpt-3.5-turbo",
        messages=[{"role": "user", "content": prompt}],
        max_tokens=500
    )

    ranked = json.loads(response.choices[0].message.content)
    ranked.sort(key=lambda x: x["score"], reverse=True)

    output = []
    for item in ranked:
        line = f"[{item['score']}] {item['idea']} (F:{item['feasibility']}/I:{item['impact']})"
        output.append(line)
        print(line)

    ranked_file.write_text("\n".join(output))

def add_idea(idea_text):
    existing = ideas_file.read_text()
    ideas_file.write_text(existing + idea_text + "\n")
    print(f"Added: {idea_text}")

def list_ideas():
    ranked = ranked_file.read_text() if ranked_file.exists() else ""
    print(ranked if ranked else ideas_file.read_text())

def pick_idea():
    ranked = ranked_file.read_text().splitlines()

    easy_ideas = [line for line in ranked if "[" in line and int(line.split("]")[0][1:]) >= 7]

    if easy_ideas:
        print("Top easy ideas:")
        for idea in easy_ideas[:3]:
            print(idea)

parser = argparse.ArgumentParser(description="AIOS Idea Ranker")
subparsers = parser.add_subparsers(dest="command", help="Commands")

rank_parser = subparsers.add_parser("rank", help="Rank all ideas")
add_parser = subparsers.add_parser("add", help="Add new idea")
add_parser.add_argument("idea", nargs="+", help="Idea text")
list_parser = subparsers.add_parser("list", help="List ideas")
pick_parser = subparsers.add_parser("pick", help="Pick easy ideas")

args = parser.parse_args()

commands = {
    "rank": rank_ideas,
    "add": lambda: add_idea(" ".join(args.idea)),
    "list": list_ideas,
    "pick": pick_idea
}

command_func = commands.get(args.command, list_ideas)
command_func()