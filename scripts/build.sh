#!/usr/bin/env bash
# Runs the native unit tests, then cross-compiles the SKSE plugin DLLs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

: "${VCPKG_ROOT:?VCPKG_ROOT not set, run scripts/setup.sh first}"
export LLVM_MINGW_BIN="${LLVM_MINGW_BIN:-$HOME/.local/opt/llvm-mingw/bin}"

"$ROOT/scripts/test.sh"

cmake --preset plugin
cmake --build --preset plugin

DLLS=("build/plugin/modules/stats/trials-of-mastery-stats.dll")

for dll in "${DLLS[@]}"; do
	# The plugins must not depend on the player's C++ runtime; fail the build if a
	# dependency or a preset change brings the dynamic CRT back.
	python3 "$ROOT/scripts/check_runtime_deps.py" "$dll"
	file "$dll"
done
echo "Built ${DLLS[*]}"
