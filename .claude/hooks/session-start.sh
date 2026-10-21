#!/bin/bash
# Claude Code cloud sessions: install the toolchain and configure build/linux
# (tools/setup-linux.sh). The session proxy serves git but not GitHub source archives,
# so glslang comes from the distribution and utf8proc from git.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "$CLAUDE_PROJECT_DIR"
WIZ8_SYSTEM_GLSLANG=1 WIZ8_GIT_SOURCES=1 tools/setup-linux.sh >&2
