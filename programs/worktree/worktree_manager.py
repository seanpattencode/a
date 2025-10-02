#!/usr/bin/env python3
import sys
import subprocess
import os
from pathlib import Path
from datetime import datetime
sys.path.append('/home/seanpatten/projects/AIOS')
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db

def create_worktree_with_terminal(repo_path=None, branch_name=None):
    """Create a new git worktree and open a terminal in it"""

    # Default to AIOS project if no repo specified
    if not repo_path or repo_path == '':
        repo_path = '/home/seanpatten/projects/AIOS'

    # Check if it's a git repository
    try:
        subprocess.run(["git", "status"], cwd=repo_path, capture_output=True, check=True)
    except:
        return {"error": "Not a git repository", "path": repo_path}

    # Generate branch name if not provided
    if not branch_name:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        branch_name = f"worktree_{timestamp}"

    # Create worktree path
    parent = Path(repo_path).parent
    worktree_path = parent / f"{Path(repo_path).name}_{branch_name}"

    # Create the worktree
    try:
        result = subprocess.run(
            ["git", "worktree", "add", "-b", branch_name, str(worktree_path)],
            cwd=repo_path,
            capture_output=True,
            text=True,
            timeout=10
        )

        if result.returncode != 0:
            return {"error": f"Failed to create worktree: {result.stderr}", "branch": branch_name}

        # Store worktree info in database
        try:
            worktrees = aios_db.read("worktrees_list")
        except:
            worktrees = []
        worktrees.append({
            "repo": str(repo_path),
            "branch": branch_name,
            "path": str(worktree_path),
            "created": datetime.now().isoformat()
        })
        aios_db.write("worktrees_list", worktrees)

        # Try to open terminal (different commands for different environments)
        terminal_opened = False
        terminal_commands = [
            ["gnome-terminal", "--working-directory", str(worktree_path)],
            ["xterm", "-e", f"cd {worktree_path} && bash"],
            ["konsole", "--workdir", str(worktree_path)],
            ["xfce4-terminal", "--working-directory", str(worktree_path)]
        ]

        for cmd in terminal_commands:
            try:
                subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                terminal_opened = True
                break
            except:
                continue

        return {
            "success": True,
            "branch": branch_name,
            "path": str(worktree_path),
            "terminal": terminal_opened
        }

    except Exception as e:
        return {"error": str(e)}

def list_worktrees():
    """List all worktrees"""
    try:
        result = subprocess.run(
            ["git", "worktree", "list"],
            capture_output=True,
            text=True
        )
        return result.stdout
    except:
        return "Not a git repository"

def remove_worktree(path):
    """Remove a worktree"""
    try:
        subprocess.run(["git", "worktree", "remove", path], check=True)
        return f"Removed worktree: {path}"
    except Exception as e:
        return f"Error removing worktree: {e}"

if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "create"

    if command == "create":
        repo = sys.argv[2] if len(sys.argv) > 2 else None
        branch = sys.argv[3] if len(sys.argv) > 3 else None
        result = create_worktree_with_terminal(repo, branch)
        if "success" in result and result["success"]:
            print(f"Created worktree: {result['path']}")
            print(f"Branch: {result['branch']}")
            if result["terminal"]:
                print("Terminal opened in worktree")
        else:
            print(f"Error: {result.get('error', 'Unknown error')}")

    elif command == "list":
        print(list_worktrees())

    elif command == "remove":
        path = sys.argv[2] if len(sys.argv) > 2 else None
        if path:
            print(remove_worktree(path))
        else:
            print("Please specify path to remove")