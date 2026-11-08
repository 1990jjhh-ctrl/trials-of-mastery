# Move Trials of Mastery onto skyrim-mod-toolchain: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Trials of Mastery's cross build, format, lint and CI run through the shared toolchain. CommonLibSSE-NG moves to the toolchain's v13.1.0, and the repo keeps no copy of the toolchain's setup, triplet, runtime check, format or lint script, or style config.

**Architecture:** [skyrim-mod-toolchain](https://github.com/1990jjhh-ctrl/skyrim-mod-toolchain) `v0.2.0` becomes the submodule `lib/skyrim-mod-toolchain`, replacing `lib/commonlibsse-ng`. This moves CommonLibSSE-NG from `8eadded` to v13.1.0 (`12d61f3`), which the toolchain PRD lists as a risk. The `plugin` preset inherits the toolchain's `skse-clangcl`. The native `core` preset (unit tests) stays the project's own. `vcpkg-configuration.json`, `.clang-format` and `.clang-tidy` become symlinks into the submodule. Usage is described in the toolchain README, "Use it in a project".

**Tech Stack:** CMake presets, vcpkg (manifest features `tests` and `plugin`), Catch2, Bash, GitHub Actions.

**Spec:** the toolchain PRD, R13 and §8 (the CommonLibSSE-NG version risk), at https://github.com/1990jjhh-ctrl/skyrim-mod-toolchain/blob/v0.2.0/docs/PRD.md. This repo's rules are in CLAUDE.md and CONTRIBUTING.md. Milestone-3 decisions: CI runs format and lint (already true here), and the shared style is this repo's own, so no reformatting is expected.

## Global Constraints

- Branch `tooling/skyrim-mod-toolchain` from `main`, named like the earlier `tooling/project-setup`. It lands through one pull request merged with a merge commit, so the history must be clean: fixes are folded into the commit they fix before merge. Conventional Commits per `.gitlint`.
- The toolchain is pinned to tag `v0.2.0` (commit `d34a05c`).
- Delete `scripts/setup.sh`, `scripts/check_runtime_deps.py`, `scripts/format.sh`, `scripts/lint.sh` and `cmake/triplets/`. Keep `scripts/test.sh` and `scripts/build.sh`; `build.sh` calls the toolchain's runtime check.
- Behaviour doesn't change. The runtime settings stay AE only (SE off, VR off). The module code (`modules/stats/plugin/src`) changes only if CommonLibSSE-NG v13.1.0 requires it, and then in its own commit.
- The `core` preset keeps working without the cross toolchain, except for the vcpkg baseline, which is the same `00c5775` through the symlink.

## Review Focus

1. **CommonLibSSE-NG version jump** (`8eadded` → v13.1.0). The plugin must still compile and link. Any API change it forces is fixed in its own `fix(stats): …` commit. Test: Task 1, Step 4.
2. **The `core` preset still runs the native tests** with the symlinked `vcpkg-configuration.json`. Test: Task 1, Step 3.
3. **Lint uses the native build.** The toolchain's `lint.sh build/core modules` must cover what the old `scripts/lint.sh` covered: only sources compiled in `build/core`. Test: Task 2, Step 2.

---

### Task 1: Toolchain submodule and build files

**Files:**
- Add: submodule `lib/skyrim-mod-toolchain` at `v0.2.0`.
- Remove: submodule `lib/commonlibsse-ng` and `cmake/triplets/`.
- Symlinks: `vcpkg-configuration.json`, `.clang-format` and `.clang-tidy`, each pointing to `lib/skyrim-mod-toolchain/<same name>`. The toolchain's `.clang-format` and `.clang-tidy` came from this repo; confirm the differences are only comments and the header filter.
- Modify: `CMakePresets.json`, `CMakeLists.txt` (the `if(TOM_BUILD_PLUGIN)` CommonLibSSE-NG block), `scripts/build.sh`.
- Delete: `scripts/setup.sh`, `scripts/check_runtime_deps.py`.

**Interfaces:**
- Presets: `"include": ["lib/skyrim-mod-toolchain/cmake/CMakePresets.json"]`. `plugin` inherits `skse-clangcl` plus the project's `TOM_BUILD_TESTS=OFF` and `TOM_BUILD_PLUGIN=ON`. Its `binaryDir` stays `build/plugin` through `build/${presetName}`. `core` and its hidden `base` stay as they are.
- CMake: inside `if(TOM_BUILD_PLUGIN)`, keep the `ENABLE_SKYRIM_*` and `BUILD_TESTS` settings, then `include(lib/skyrim-mod-toolchain/cmake/commonlibsse.cmake)` in place of `add_subdirectory`. `modules/stats` links `CommonLibSSE::CommonLibSSE` as today.
- `build.sh` calls `python3 lib/skyrim-mod-toolchain/scripts/check_runtime_deps.py "${DLLS[@]}"`. Its hint names `lib/skyrim-mod-toolchain/scripts/setup.sh`.

- [ ] **Step 1:** Make the changes, `git submodule update --init --recursive`, and grep for leftovers: `grep -rn 'lib/commonlibsse-ng\|cmake/triplets\|scripts/setup.sh\|check_runtime_deps' --exclude-dir=lib --exclude-dir=build --exclude-dir=docs .`.
- [ ] **Step 2:** Export `VCPKG_ROOT`, `XWIN_SYSROOT` and `LLVM_MINGW_BIN` as setup prints them.
- [ ] **Step 3:** Run `scripts/test.sh`. Expected: the `core` preset configures, builds and passes.
- [ ] **Step 4:** Run `rm -rf build/plugin`, then `scripts/build.sh` in the background. Expected: `build/plugin/modules/stats/trials-of-mastery-stats.dll` exists and the runtime check prints `OK`. If v13.1.0 breaks the plugin source, make the smallest fix in `modules/stats/plugin/src` as its own commit.
- [ ] **Step 5: Commit** `build: take CommonLibSSE-NG and the build setup from skyrim-mod-toolchain`, plus the separate `fix(stats): …` if Step 4 needed one.

### Task 2: Format and lint through the toolchain

- [ ] **Step 1:** Delete `scripts/format.sh` and `scripts/lint.sh`. Point `.vscode/tasks.json` at `lib/skyrim-mod-toolchain/scripts/format.sh` (arguments `modules`, or `--check modules`) and at `lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules`. Keep the problem matchers.
- [ ] **Step 2:** Run `lib/skyrim-mod-toolchain/scripts/format.sh --check modules` and `lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules`. Expected: both clean, and lint checks the same files the old script did (those under `modules/` in `build/core`).
- [ ] **Step 3: Commit** `build: format and lint with the toolchain's scripts`.

### Task 3: CI and docs

**Files:**
- Modify: `.github/workflows/ci.yml`, `README.md` ("Setup", "Build and test"), `CONTRIBUTING.md` (workflow step 3 and the CI list), `CLAUDE.md` (code-style link only if it changes).

**Interfaces:**
- The `checks` job (ubuntu-24.04, `timeout-minutes: 90`):
  1. `actions/checkout@v7` with `submodules: recursive`.
  2. `uses: ./lib/skyrim-mod-toolchain/.github/actions/setup` with `native-tests: "true"`.
  3. `scripts/test.sh`.
  4. Format check: `lib/skyrim-mod-toolchain/scripts/format.sh --check modules`.
  5. Lint: `lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules`.
  6. `scripts/build.sh`, which now runs in CI too.

  Remove the job's own ninja, llvm-mingw, vcpkg and cache steps and the comments about them. The `commits` job is unchanged.

- [ ] **Step 1:** Edit the workflow. Update its header comment: the cross build now runs in CI.
- [ ] **Step 2:** Update the docs:
  - README: setup is `lib/skyrim-mod-toolchain/scripts/setup.sh`, linked to the toolchain README's "Use it in a project" instead of restating it. Show the format and lint commands with their arguments.
  - CONTRIBUTING: list the new local checks and the CI steps, and drop the sentence saying the cross build stays local.
  - Keep every relative link resolving.
- [ ] **Step 3: Commit** `ci: run the checks and the cross build through the toolchain's setup action`, then `docs: describe the toolchain-based setup`.

### Task 4: Verify on GitHub

- [ ] **Step 1:** Push the branch and open a pull request against `main`. The body lists the tasks and the CommonLibSSE-NG version change, and ends with the Claude Code line.
- [ ] **Step 2:** Watch CI until it's green. Fix failures with `--fixup` commits and plain pushes. Never force-push and never merge. The fixups are squashed with the user's approval before the merge commit.
