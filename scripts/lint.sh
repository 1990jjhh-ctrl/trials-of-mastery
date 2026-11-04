#!/usr/bin/env bash
# Runs clang-tidy (.clang-tidy) on the project's C++ sources.
#
#   scripts/lint.sh [build dir]   default: build/core
#
# Needs the compile database of a configured build (scripts/test.sh configures
# build/core). Uses llvm-mingw's clang-tidy, which setup.sh installs at a pinned
# version, so every machine reports the same findings. Override with
# CLANG_TIDY=/path/to/clang-tidy. Only modules/ is checked; lib/ is a submodule.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD="${1:-build/core}"
if [[ ! -f "$BUILD/compile_commands.json" ]]; then
	echo "no $BUILD/compile_commands.json; configure first (scripts/test.sh)" >&2
	exit 2
fi

CT="${CLANG_TIDY:-}"
if [[ -z "$CT" ]]; then
	for candidate in "${LLVM_MINGW_BIN:-$HOME/.local/opt/llvm-mingw/bin}/clang-tidy" "$(command -v clang-tidy || true)"; do
		if [[ -n "$candidate" && -x "$candidate" ]]; then
			CT="$candidate"
			break
		fi
	done
fi
if [[ -z "$CT" || ! -x "$CT" ]]; then
	echo "clang-tidy not found; run scripts/setup.sh or set CLANG_TIDY" >&2
	exit 2
fi

# Only the sources this build compiles: the compile database has no entry for
# the others (the plugin sources are missing from build/core, for example).
mapfile -t FILES < <(python3 -c '
import json, sys
for entry in json.load(open(sys.argv[1])):
    path = entry["file"]
    if "/modules/" in path:
        print(path)
' "$BUILD/compile_commands.json" | sort -u)

if "$CT" -p "$BUILD" --quiet "${FILES[@]}"; then
	echo "lint: ${#FILES[@]} files clean ($("$CT" --version | grep -m1 -o 'version [0-9.]*'))"
else
	echo "lint: findings above" >&2
	exit 1
fi
