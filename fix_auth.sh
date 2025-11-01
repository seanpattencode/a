#!/bin/bash
# Quick script to convert all HTTPS remotes to SSH for aio compatibility

echo "🔧 Converting all HTTPS remotes to SSH..."
echo "========================================="

# Array of projects and their paths
declare -a projects=(
    "/home/seanpatten/AndroidStudioProjects/Workcycle"
    "/home/seanpatten/projects/alpha"
    "/home/seanpatten/projects/aicombo"
    "/home/seanpatten/projects/aischedulerdemo"
    "/home/seanpatten/projects/monitorControl"
)

for project_path in "${projects[@]}"; do
    if [ -d "$project_path/.git" ]; then
        project_name=$(basename "$project_path")
        echo -n "Converting $project_name... "

        # Get current remote URL
        current_url=$(git -C "$project_path" remote get-url origin 2>/dev/null)

        if [[ "$current_url" == https://github.com/* ]]; then
            # Extract username/repo from HTTPS URL
            # https://github.com/username/repo.git -> git@github.com:username/repo.git
            ssh_url=$(echo "$current_url" | sed 's|https://github.com/|git@github.com:|')

            # Set new SSH URL
            git -C "$project_path" remote set-url origin "$ssh_url"
            echo "✅ Converted to SSH"
        elif [[ "$current_url" == git@github.com:* ]]; then
            echo "✓ Already using SSH"
        else
            echo "⚠️  Unknown remote format: $current_url"
        fi
    else
        echo "⚠️  Skipping $(basename "$project_path") - not a git repository"
    fi
done

echo ""
echo "✅ Done! Now run 'aio all' and all projects will work!"