# Workflow Manager - Example Usage

## Scenario: Building a Multi-Feature App with Parallel Execution

### 1. Start with High-Level Ideas (Column 0)

```bash
python3 programs/workflow/workflow.py add 0 "Build authentication system"
# Output: 0

python3 programs/workflow/workflow.py add 0 "Create user dashboard"
# Output: 1

python3 programs/workflow/workflow.py add 0 "Implement payment processing"
# Output: 2
```

### 2. Expand Ideas into Detailed Steps (Column 1)

```bash
# Expand auth system into specific tasks
python3 programs/workflow/workflow.py expand 0 "1. JWT tokens 2. Login endpoint 3. Password hashing with bcrypt"
# Output: 3

# Create alternative approach
python3 programs/workflow/workflow.py expand 0 "Alternative: Use OAuth2 with Google/GitHub"
# Output: 4

# Expand dashboard
python3 programs/workflow/workflow.py expand 1 "React dashboard with charts using recharts library"
# Output: 5
```

### 3. Add Comments and Context

```bash
python3 programs/workflow/workflow.py comment 3 "Standard JWT approach - battle tested"
python3 programs/workflow/workflow.py comment 4 "OAuth2 might be overkill for MVP"
python3 programs/workflow/workflow.py comment 5 "Recharts is lightweight and TypeScript-friendly"
```

### 4. Create Parallel Execution Branches

```bash
# Create separate worktrees for parallel work
python3 programs/workflow/workflow.py branch 3 worktree
# Creates: /path/to/project-workflow-3 as git worktree

python3 programs/workflow/workflow.py branch 4 worktree
# Creates: /path/to/project-workflow-4 as git worktree

# Or use subfolders for non-git workflows
python3 programs/workflow/workflow.py branch 5 subfolder
# Creates: /path/to/project/workflow-5/
```

### 5. Execute and Work

```bash
# Execute node as a job (integrates with AIOS job system)
python3 programs/workflow/workflow.py exec 3

# Open terminal to work manually
python3 programs/workflow/workflow.py term 3 gnome-terminal
# or
python3 programs/workflow/workflow.py term 3 code  # Opens in VSCode

# Make changes, then push
python3 programs/workflow/workflow.py push 3 "Implemented JWT authentication"
```

### 6. Save and Reuse Workflows

```bash
# Save current workflow
python3 programs/workflow/workflow.py save auth-feature-workflow

# Later, load it back
python3 programs/workflow/workflow.py load auth-feature-workflow

# Create workflow templates with placeholders
python3 programs/workflow/workflow.py add 0 "Implement {FEATURE_NAME} with {TECHNOLOGY}"
python3 programs/workflow/workflow.py save feature-template
```

### 7. View Progress

```bash
python3 programs/workflow/workflow.py list
```

Output:
```
Column 0:
  0: Build authentication system
  1: Create user dashboard
  2: Implement payment processing
Column 1:
  3: Build authentication system
1. JWT tokens 2. Login endpoint 3. Password hashing
  4: Build authentication system
Alternative: Use OAuth2 with Google/GitHub
  5: Create user dashboard
React dashboard with charts using recharts
```

## Web UI Usage

1. Start AIOS: `python3 aios_start.py`
2. Navigate to: `http://localhost:8080/workflow`
3. Click nodes to expand/branch/execute/comment
4. Save/load workflows with one click

## Advanced: Multi-Level Abstraction

```bash
# Level 0: Business goal
python3 programs/workflow/workflow.py add 0 "Launch SaaS product"

# Level 1: Major features
python3 programs/workflow/workflow.py expand 0 "Auth, Dashboard, Billing, API"

# Level 2: Detailed tasks
python3 programs/workflow/workflow.py expand 3 "JWT implementation: Setup passport.js, Create login route, Add refresh tokens"

# Level 3: Code-level steps
python3 programs/workflow/workflow.py expand 6 "npm install passport passport-jwt; Create config/passport.js; Add auth middleware"

# Execute at any level
python3 programs/workflow/workflow.py exec 6  # Most detailed
python3 programs/workflow/workflow.py exec 3  # Higher level
```

## Integration with AIOS Jobs

Executing a workflow node creates a job in the AIOS system:

```bash
python3 programs/workflow/workflow.py exec 3
# Creates job in jobs table with status 'running'

# Check job status
python3 services/jobs.py list

# View in web UI
http://localhost:8080/jobs
```

## Performance

All commands complete in < 0.1 seconds, making this suitable for:
- Interactive CLI workflows
- Automation scripts
- CI/CD pipelines
- Real-time collaboration tools
