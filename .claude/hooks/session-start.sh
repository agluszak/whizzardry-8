#!/bin/bash
# Claude Code cloud sessions: install the toolchain and configure build/linux
# (tools/cloud-deps.sh). Project threads don't run repository hooks; they follow
# CLAUDE.md instead.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "$CLAUDE_PROJECT_DIR"
tools/cloud-deps.sh >&2
