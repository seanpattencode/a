# a rescue — PRELIMINARY: activity→adata/local + salvage unpushed non-activity (HSU+fedora 8/30). 100G+ .git: re-clone 1st.
set -e
cd ~/a && git pull --ff-only && sh a.c
cd adata/git; exec 9>/tmp/.a_git.lock; flock 9
git fetch -q origin main
git branch rescue-bak 2>/dev/null || :
git diff origin/main...HEAD -- . ':!activity' >/tmp/rp
git reset -q --soft origin/main && git read-tree origin/main
git ls-files -z activity|git update-index --skip-worktree -z --stdin
[ ! -s /tmp/rp ]||{ git apply --cached /tmp/rp&&git commit -qm rescue&&git push origin main;}
git diff --name-only -z -- . ':!activity'|xargs -0 -r git restore --worktree --
cd .. && a actmig
echo "rescued $(git -C git rev-list --left-right --count origin/main...HEAD|tr '\t' /) u=$(git -C git ls-files -o --exclude-standard activity|wc -l)"
