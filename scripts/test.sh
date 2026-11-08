#!/usr/bin/env bash
# Native unit tests of the core libraries (no game, no cross-compiler).
# Extra arguments go to ctest, e.g. scripts/test.sh -R "<test name>".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

: "${VCPKG_ROOT:?VCPKG_ROOT not set, run lib/skyrim-mod-toolchain/scripts/setup.sh first}"

cmake --preset core
cmake --build --preset core
ctest --preset core "$@"
