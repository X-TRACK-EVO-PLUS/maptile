#!/usr/bin/env bash
# Install the repository git hooks by pointing core.hooksPath at .githooks.
# Run once after cloning:  tools/install-hooks.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
chmod +x .githooks/* 2>/dev/null || true
git config core.hooksPath .githooks
echo "git hooks installed (core.hooksPath = .githooks)"
echo "pre-commit enforces clang-format (WebKit) on staged C/C++ files."
