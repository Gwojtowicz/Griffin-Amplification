#!/usr/bin/env bash
# Applies Griffin Amplification's library patches to the pinned submodules.
# Safe to run more than once: a patch that is already applied is skipped.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
git -C "$root" submodule update --init --recursive
for lib in iPlug2 NeuralAmpModelerCore AudioDSPTools; do
  patch="$root/patches/$lib.patch"
  if git -C "$root/$lib" apply --reverse --check --whitespace=nowarn "$patch" 2>/dev/null; then
    echo "$lib: already applied"; continue
  fi
  git -C "$root/$lib" apply --whitespace=nowarn "$patch"
  echo "$lib: applied"
done
