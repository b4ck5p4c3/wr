#!/bin/sh
# Install the project git hooks. The pre-commit hook is symlinked from the
# tracked hooks directory, so a hook update rides the repository rather than a
# manual copy. The git-path query resolves the real hooks directory even from a
# worktree or a submodule, where .git is a file rather than a directory.
set -e

repo_root=$(git rev-parse --show-toplevel)
hooks_dir=$(git rev-parse --git-path hooks)

mkdir -p "$hooks_dir"
chmod +x "$repo_root/hooks/pre-commit"
ln -sf "$repo_root/hooks/pre-commit" "$hooks_dir/pre-commit"

echo "Installed the pre-commit hook, make fmt now runs before every commit."
