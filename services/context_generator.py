from pathlib import Path
from datetime import datetime
def generate():
    root = Path(__file__).parent.parent
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    files = list(root.rglob("*.py"))
    readme = root / "README.md"

    code_files = [f for f in files if "testing" not in str(f)]
    test_files = [f for f in files if "testing" in str(f)]

    base_content = f"Generated: {timestamp}\n\n"
    base_content += "\n".join([f"{f.relative_to(root)}:\n{f.read_text()}\n" for f in code_files])
    base_content += (lambda r: f"\nREADME.md:\n{r.read_text()}\n" * r.exists())(readme)

    full_content = base_content + "\n".join([f"{f.relative_to(root)}:\n{f.read_text()}\n" for f in test_files])

    (root / "projectContext.txt").write_text(base_content)
    (root / "projectContextWithTests.txt").write_text(full_content)

    return str(root / "projectContext.txt")