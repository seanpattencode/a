# Workflow Manager - Idea to Execution

Fast terminal-based workflow manager for managing multi-level abstraction execution pipelines with parallel branches.

## Terminal Commands (All < 0.1s)

```bash
# Node Management
python3 programs/workflow/workflow.py add <col> <text>        # Add node at column/abstraction level
python3 programs/workflow/workflow.py list                     # Show all nodes grouped by column
python3 programs/workflow/workflow.py expand <id> [details]    # Create child node in next column

# Branch Management
python3 programs/workflow/workflow.py branch <id> [type]       # Create worktree/subfolder/main branch
python3 programs/workflow/workflow.py exec <id>                # Execute node as job

# Workflow Save/Load
python3 programs/workflow/workflow.py save <name>              # Save current workflow
python3 programs/workflow/workflow.py load <name>              # Load saved workflow

# Integration
python3 programs/workflow/workflow.py push <id> [msg]          # Git add/commit/push from node path
python3 programs/workflow/workflow.py term <id> [terminal]     # Open terminal at node path
python3 programs/workflow/workflow.py comment <id> <text>      # Add comment/note to node
```

## Web UI

Navigate to `/workflow` in browser when AIOS is running.

**Features:**
- Visual column-based node display (abstraction layers)
- Click nodes to expand/branch/execute/comment
- Save/load workflows with one click
- 7 lines of JavaScript (< 50 line requirement)
- Minimal modern aesthetic

## Architecture

**Columns = Abstraction Layers**
- Left: High-level user prompts
- Right: Detailed execution steps
- Each node can expand to next column (more detailed)

**Parallel Execution**
- Each branch runs in: worktree / subfolder / main folder
- Graph tracks parent-child relationships
- Multiple attempts run simultaneously

**Data Storage**
- Nodes: `workflow_nodes` in aios.db
- Workflows: `workflows` in aios.db
- Comments: Stored in node objects

## Performance

- All commands: < 0.1s timeout ✓
- workflow.py: 118 lines
- workflow.html: 30 lines (7 JS lines)
- web.py additions: +18 lines
- Load time: ~0.012s

## Example Usage

```bash
# Start with high-level idea
python3 programs/workflow/workflow.py add 0 "Build user auth system"

# Expand to detailed steps
python3 programs/workflow/workflow.py expand 0 "1. Create login form 2. Add JWT tokens 3. Test auth flow"

# Create parallel attempts
python3 programs/workflow/workflow.py branch 1 worktree
python3 programs/workflow/workflow.py branch 1 worktree

# Execute and comment
python3 programs/workflow/workflow.py exec 1
python3 programs/workflow/workflow.py comment 1 "Using bcrypt for password hashing"

# Save for reuse
python3 programs/workflow/workflow.py save auth-workflow
```
