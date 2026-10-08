#!/usr/bin/env bash
# Run after committing an upstreamable change on top of the branch.
# Moves HEAD (an upstreamable commit) below the whizzardry-only commits.
# Upstream commits end at tag upstream-tip; whizzardry commits follow it.
set -euo pipefail
branch=$(git symbolic-ref --short HEAD)
new=$(git rev-parse HEAD)
local_commits=$(git rev-list --reverse upstream-tip..HEAD~1)
git checkout -q --detach upstream-tip
git cherry-pick "$new" >/dev/null
git tag -f upstream-tip HEAD >/dev/null
for commit in $local_commits; do git cherry-pick "$commit" >/dev/null; done
git branch -f "$branch" HEAD
git checkout -q "$branch"
git log --oneline --decorate -n $(( $(echo $local_commits | wc -w) + 3 ))
