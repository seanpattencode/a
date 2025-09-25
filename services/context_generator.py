from pathlib import Path
from datetime import datetime

def generate():
    root = Path(__file__).parent.parent
    output = Path(root / "projectContext.txt")
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    files = [f for f in root.rglob("*.py") if "archive" not in f.parts]

    content = f"Generated: {timestamp}\n\n"
    content += "\n".join([f"{f.relative_to(root)}:\n{f.read_text()}\n" for f in files])

    output.write_text(content)
    return str(output)