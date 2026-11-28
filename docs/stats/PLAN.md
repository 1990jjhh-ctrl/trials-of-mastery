# Trials of Mastery Stats: Implementation Plan

Status: APPROVED
Date: 2026-11-04
Implements: [Stats PRD](PRD.md) (ST-1 to ST-28). Shared rules: [overview](../PRD.md).

This plan does not restate requirements. Each task names the requirement IDs it covers. The PRD holds what they mean.

## Goal

Build the Stats module as an SKSE plugin for Skyrim AE. The formula and configuration core is a library with no game dependency, tested by unit tests outside the game ([success criterion 4](PRD.md#3-goals-and-success-criteria)). A separate game layer connects that core to the game, the stat box, the C++ and Papyrus API and compatibility detection.

## Conventions used in this plan

- **OQ-n** is the n-th bullet in [PRD §10 Open questions](PRD.md#10-open-questions): OQ-1 NPC values, OQ-2 default effect coefficients, OQ-3 skill XP split, OQ-4 learning-speed curve, OQ-5 check weights, OQ-6 END redirection share, OQ-7 direct activity XP amounts, OQ-8 warn or disable on an incompatible mod (closed, see below), OQ-9 STR XP for unarmed attacks by glove weight, OQ-10 PER XP for fighting, OQ-11 END's resistance to poison, fall damage mitigation and breath duration (their default curves), OQ-12 sneak reveal (the cue, the range curve from PER, line of sight); OQ-11 and OQ-12 come with PR #55, which also adds the default spell crit multiplier (ST-13) to OQ-2. **OQ-8 is closed** (user decision, 2026-11-27, PR #45): a detected incompatible mod disables the module (ST-28), and the bullet left the PRD. The IDs keep their numbers, so the `TODO(OQ-9)` and `TODO(OQ-10)` markers in the data stay valid; from OQ-9 on, OQ-n is the (n − 1)-th bullet of the PRD list. A task that depends on an OQ ships a **placeholder default** in a config file and marks it `TODO(OQ-n)` there and in the log at load. The plan does not answer any OQ.
- **Verification types:** `unit` means a ctest test that runs on the dev host without the game. `log` means a check of the plugin log (`Documents/My Games/Skyrim Special Edition/SKSE/<plugin>.log`). `in-game` means a manual step list that the task adds to `modules/stats/tests/ingame/<task-id>.md`. In-game checks call the Papyrus API from the console once A2 exists, through the console route A2 sets up (`cgf` is not in Skyrim SE, [S8 result](#s8-result)). Before A2 they use log dumps. Every check that starts the game, `in-game` or a `log` check of a game run, including the spike runs, runs from the main session only ([CLAUDE.md#workflow](../../CLAUDE.md#workflow)). An implementer writes the step list and reports the check as not run; the main session runs it and records the result.
- **Parallel** means the tasks share no files except one-line additions to a CMake source list.
- Code style: the user's `write-code` skill applies to all C++ and Papyrus, unless the project states a deviation.

## Decisions taken

These are the user's answers to the decisions in the first draft of this plan.

| ID | Decision |
| --- | --- |
| D1 | CommonLibSSE-NG (fork `alandtse/CommonLibSSE-NG`, branch `ng`), built from the skyrim-mod-toolchain submodule under `lib/skyrim-mod-toolchain` ([README.md#setup](../../README.md#setup)) |
| D2 | No MSVC. The plugin is cross-built on Linux with clang-cl 21 and lld-link against the xwin splat (VC and Windows Kits) in `/home/joel-hirsbrunner/xwin-out`. CMake and ninja are installed. vcpkg comes from `$VCPKG_ROOT`, not from a submodule. The plugins link the C++ runtime statically (/MT), checked after each build |
| D3 | CMake |
| D4 | Catch2, nlohmann/json, SimpleIni |
| D5 | The Creation Kit Papyrus compiler under Wine (`<Skyrim>/Papyrus Compiler/PapyrusCompiler.exe`), with the vanilla sources and `TESV_Papyrus_Flags.flg` from `<Skyrim>/Data/Source/Scripts`. Caprica is not used |
| D6 | Assumptions confirmed or moved into the PRDs; see below |
| D7 | The stat box opens with key K and gamepad button Y by default (user decision, 2026-11-24). ST-24 asks for a key and a button in the INI but names neither |
| D8 | U2, the skills-menu hint of ST-25 (Could), is dropped for v1 (user decision, 2026-11-27), since the HUD route of S2 does not work ([S2 result](#s2-result)) |
| D9 | A hook whose site is not found, or that cannot be installed, is only logged: its feature is off for that session, with no message box. User decision of 2026-11-27 for G6 (the learning bonus), extended on 2026-11-28 to G7, G7a, G7b, G8, G9 and G10 |

## Assumptions and open questions

1. **Repo layout (confirmed):** `modules/stats/` with `core/` (game-independent library), `plugin/` (SKSE DLL), `include/tom/stats/` (public C++ API headers), `papyrus/` (script sources), `data/` (default INI and JSON), `tests/` (unit and in-game). The top-level `CMakeLists.txt` adds the module.
2. **Install paths (confirmed):** one DLL and one INI per module, sharing one package folder:

   ```txt
   Data/SKSE/Plugins/
   +-- trials-of-mastery/
   |   +-- data/
   |   |   +-- stats/{stats,skills,actions,incompatible}/
   |   +-- LICENSE.txt
   |   +-- README.txt
   +-- trials-of-mastery-stats.dll
   +-- trials-of-mastery-stats.ini
   ```

   The compiled Papyrus script goes to `Data/Scripts/`, where the game looks for it. Later modules add their DLL, INI and `data/<module>/` folder next to these.
3. **API registrations against JSON (confirmed):** API-registered entries are the lowest layer of the table merge ([overview §4](../PRD.md#4-shared-constraints)).
4. **XP modifier combination (confirmed):** each registered XP modifier (ST-17) is a factor `1 + m` under its owner's key, and all factors multiply.
5. **Ascend through Papyrus (confirmed):** the ascend math lives in C++. The Papyrus binding only passes on a [curve](../PRD.md#41-curves) name and its parameters.
6. **Numeric types:** base, point counts and modifiers are signed 32-bit integers. XP, cost, multiplier and effective stat are `double`.
7. **Check-to-result mapping (OQ-5):** each check in the actions file gets an output mapping from `I` to the game quantity (sweet-spot size, pickpocket chance, price factor). The mapping is a curve with placeholder parameters.
8. **Registered stats whose owner is missing at load:** their saved data is kept but dormant. It is not shown and has no effects, and it comes back when the owner registers again. The PRD does not cover this case.
9. **Custom Skills Framework (CSF):** settled for CSF 3.2.0 by the [S7 result](#s7-result): CSF announces its level-ups through its SKSE interface (G4), and its skill XP goes through its own `Skill::Advance`, where G6 multiplies it after CSF's Mod Skill Use perk entry. Other CSF versions are not covered; G4 and G6 log and skip what they do not recognize. A CSF skill's key in the skills file and for `trainSkillLevelUp` and `skillXpFactor` is its name from [§7.8](PRD.md#78-configuration) (*user decision, 2026-11-28*, PR #55); G4 and G6 build it in one helper. Which CSF level changes train is set by [ST-16](PRD.md#74-training-and-actions) (same decision); G4 follows it.
10. **Game version (confirmed, [overview §4](../PRD.md#4-shared-constraints)):** 1.6.x and newer, tested on 1.7.104. T1.2 checks the Address Library and CommonLibSSE-NG support for 1.7.104.
11. **Base adjustments ([ST-3](PRD.md#71-stats)):** an actor's stored base already contains its adjustments, and each adjustment is stored under its key. Setting a key changes the base by the difference to the key's old value; removing a key takes its value off the base. The race adjustment is one such key, owned by the module and not settable through the API. When the stats table's race adjustment for an actor's race differs from the stored one (a patch changed, or the actor changed race), the module sets the key again, so the base and with it the effects change by the difference (ST-3). A changed **default base** reaches existing actors the same way, as ST-3 and [§8](PRD.md#8-constraints) define: each actor stores the default base last applied per stat, and the module moves the base by the difference.
12. **Effect reference ([ST-9, ST-10](PRD.md#73-effects)):** relative effects measure from the default base last applied to the actor's stat (`ActorStats::defaultBases`, C4), which is the stored form of "default base" for an existing actor. A stat with no default base applied counts it as 0, like any missing value in `ActorStats`; So C7 never gives a stat a non-zero base without `initStat`, and C8 restores the default base last applied together with every base.
13. **Open: default-base patch at a multiplier other than 1.** ST-3 says a patch that changes a default base leaves relative effects unchanged, since base and default base move together. With `x = effective − default base` and `effective = (base + …) · multiplier + modifier`, a change `d` of both moves `x` by `d · (multiplier − 1)`, so this holds only at multiplier 1. The plan checks it at multiplier 1 only and does not settle the other case; it is for the user to decide in the PRD (ST-3, ST-8, MA-3).
14. **Tables fixed at construction (constraint, from C7):** `StatsService` takes its `Tables` in the constructor and never changes them, and `registerStat` fails for a stat without a stats-table entry, since the default base comes from there (ST-3). So every table entry, the API-registered ones included (A1, the luck module's LUC per [LU-1](../luck/PRD.md)), is merged and passed to `buildTables` before the service is built; G1 builds it after that, then calls `registerStat` for each registered stat. `registerStat` on a built service only activates a stat whose entry is already in the tables; a stat whose entry arrives later cannot be added in that session.
15. **Threads (settled in G1, PR #57):** `StatsService` has no locking. Its copy-on-write crit shifts (C7) protect only against re-entry on one thread: a shift that sets or removes shifts while `critChance` runs. Two threads calling the service at once are a data race. G1 set the thread rule for the whole plugin, which each hook task, A1 and A2 follow; it is written in [Service.h](../../modules/stats/plugin/src/Service.h). What the [S7 result](#s7-result) logged: the plugin installs on the thread of `kDataLoaded` (588 and 576 in two runs). Console commands from the skyrim-mcp bridge ran on other threads (440, 380), and the skill XP hooks and the vanilla `SkillIncrease::Event` ran on the thread of the command. A task queued with `SKSE::GetTaskInterface()->AddTask` ran on yet another thread (376), on which CSF's `Advance`, its perk entry and its `SkillIncreaseEvent` ran. So the hooks and sinks run on whatever thread triggers the skill XP, which need not be the install thread.

    What the spike runs logged, for the hook tasks (thread IDs are of one game session each):

    - [S4](#s4-result): the damage hooks of melee hits ran on worker threads (436 to 448), those of a Firebolt's ticks on the thread that received `kPostLoad` (380), fall damage on a worker (440); the hooks were installed on the `kDataLoaded` thread (584).
    - [S5](#s5-result): the physical crit hooks ran on worker threads (428 to 440); the spell hook on workers and, for a fire hazard, on the `kPostLoad` thread (388), which also ran the effect ticks.
    - [S6a](#s6a-result): `PlayerCharacter::Update` and `HandleHealthDamage` ran on the thread that received `kPostLoad` (376), as the effect ticks did in S4 and S5; the SKSE messages `kPostLoad`, `kInputLoaded`, `kPreLoadGame` and `kPostLoadGame` arrive there too (all runs), and so did a task queued with `AddTask` in the S7 run. So that thread is the game's main loop; `kDataLoaded` arrives on another one (576 or 584), which the prototypes' logs call "main" by mistake. Animation events, hits, topic info and the resistance check ran on workers (428 to 440).
    - [S6b](#s6b-result): detection and the dialogue conditions ran on worker threads (428 to 440).
    - [S8](#s8-result): the natives were registered on thread 548 at VM init; called with `DispatchStaticCall` from `kPostLoadGame` on the main loop's thread (376), they ran on 436.
16. **N and claims are session state (user decision, 2026-11-26):** as [§8](PRD.md#8-constraints) has it, a save stores `r` but neither N nor the claims of advancing (ST-6) and point assignment (ST-22). N comes from the INI each session, so a changed N applies to existing saves, and a claiming module (the mastery module) sets it again through the API. The claiming module also claims again each session. So C8 stores neither, and a revert keeps both, like crit shifts and XP modifiers.
17. **Stat box display (from PRs #31 and #33, C3a, C3b, C9a, C9b, C9c):** ST-21 and ST-24 leave these details open. The ones marked *user decision* are the user's answers of 2026-11-26; the others are plan choices:
    - *User decision:* "as in the stats file" means the order in which the stats files of the merge (default, patches, mod patches, in merge order, each in its text order) first name a stat, with each stat's group taken from the merged table. A group set only by an API-registered entry is not a stats-file group. The shipped default is reordered so its groups come out as [§7.8](PRD.md#78-configuration) lists them: Warrior, Mage, Thief, Other.
    - *User decision:* a registered stat that names a group no stats file names (neither a stats-file group nor its namespace group) is ordered with the namespace groups, by its registration, before Other.
    - Within a section the stats keep the registry order (core stats in PRD order, then the registered ones in registration order). A section is the group name on its own line, then its lines; sections are separated by an empty line. A group with no active stat has no section, also when its namespace has a section provider (*user decision*).
    - A section provider fills the section whose group name equals its namespace, however the group was set. It is called once per opening, so lines a Papyrus module updates while the box is open show at the next opening. A provider fills only the overview; the group's stats still get their buttons on the Level Up pages.
    - Rounding: XP rounds down, and the cost rounds up after the tolerance of `addXp` is taken off, so the box never shows `100 / 100` without the point gained. *User decision:* the effective value can have a fraction (multiplier, MA-3); it shows as a whole number rounded down, where C9 showed one decimal.
    - *User decisions for the pages of PR #33 (C9c):* the lines per page count every line of a page's text, the group names and the empty lines between sections included. "Back" is the leftmost button on every level-up page, paged or not, so merged C9's "Back" moves from last to first there: on the first page of a list it leaves the list as before, from the second page on it goes to the previous page. The paged group list is headed `Level Up (1 / 2)`.
    - *Plan choices for the pages of PR #33 (C9c):* a page fills with whole sections in order; a section that does not fit on the current page starts a new one. A section longer than a page gets pages of its own: `k = ⌈n / (L − 1)⌉` pages for `n` lines and `L` lines per page, the first `n mod k` of them one line longer. A paged button list has `k = ⌈n / (B − 2)⌉` pages for `n` buttons and `B` buttons per box, spread the same way, with room for "Back" and "Next". Button rows: "Back" first, then the page's own buttons, then "Next" last, on the overview (ST-24) and on the level-up pages alike. "Back" from "Level Up" returns to the overview page it was opened from.
    - *Plan choice:* ST-24 names no suffix after T; from 1000T on the box shows the whole number of T (`2500T`). NaN and −∞, which no stat value should reach, show as `?`.
18. **Compatibility warning (from PRs #43 and #45, C3c, C10, K1):** [ST-27 and ST-28](PRD.md#79-compatibility) set the buttons, the per-mod dismissal through an INI list, that a dismissed mod still disables the module and is logged, that with the INI override the module runs and shows no warning while the log still records the detected mods, and that a box that cannot show at the main menu shows when the player first starts or loads a game (user decisions, 2026-11-27). A detected mod disables the module (OQ-8, closed). The rest are plan choices, not user decisions:
    - The list is the single INI setting `[Compatibility] DismissedWarnings`, so ST-27's "only single settings" holds: one key whose value is a comma-separated list of incompatible-file entry names (the JSON keys, such as `Requiem` or `Valhalla Combat`, which the log and the box show), e.g. `DismissedWarnings = Requiem, Valhalla Combat`. Each item is trimmed of spaces and tabs; empty items are left out and a repeated item counts once. An item matches an entry name exactly (case-sensitive, as JSON keys are). An item that names no entry of the current table is kept in the list and ignored, so a dismissal survives a table patch that drops the entry for a while. The shipped INI has the key with an empty value.
    - So the list stays unambiguous, an incompatible entry name must not be empty, contain a comma or a control character, or start or end with whitespace; the C3 validator rejects such an entry, and its file is skipped (C3c).
    - The box names only the detected mods that are not dismissed, and is not shown when every detected mod is dismissed, nor at all with the override set. The log lists every detected mod at every load, each marked dismissed or not, and says whether the module is disabled or runs through the override.
    - Buttons: "OK" first, then "Don't show again". "OK" changes nothing. "Don't show again" adds the mods the box named to the list (the existing items keep their order and text, new ones are appended) and writes the list back.
    - Write-back: the plugin reads the INI file from disk again at the press, so edits made since the load stay, changes only the `DismissedWarnings` line of `[Compatibility]` by a text edit (C10's `setIniValue`; every other line, comment and line ending stays as it was) and writes the file through a temporary file in the same folder that then replaces it. Without an INI file, it writes a new one with only that section and key. A failed write is logged; the box closes as usual and the warning shows again at the next load.
    - The box is the message box of U1's adapter (`verticalButtons = true`); ST-28 does not say how its buttons are laid out.
    - Escape and the gamepad's cancel button on the warning stay open (assumption 19).
19. **Escape and the gamepad's cancel button in a message box (open; user decision 2026-11-27: U1 and K1 report what the game does first, the user decides afterwards):** ST-21 and ST-24 close the stat box only through its buttons ("OK", or "Level Up" and "Cancel"), and ST-28 names only "OK" and "Don't show again" for the warning. Neither says what Escape or the gamepad's cancel button (B) does while a box is open. The factory's `MessageBoxData` has `isCancellable` false and `cancelButtonIndex` −1 ([S2 result](#s2-result)); U1 and K1 keep these defaults and record what happens (in-game checks), and the expected behaviour is the user's to decide.

## Tasks

### Phase 1: Setup

#### T1.1 Build and test skeleton

- **Goal:** CMake project with the core library target, the unit-test target and a placeholder plugin target that cross-builds with clang-cl. Dependencies come from vcpkg, CommonLibSSE-NG from its submodule.
- **Covers:** success criterion 4 (infrastructure).
- **Scope:**
  - top-level `CMakeLists.txt` and `CMakePresets.json`;
  - `vcpkg.json` and `vcpkg-configuration.json` (pinned baseline), for the vcpkg in `$VCPKG_ROOT`;
  - the CommonLibSSE-NG submodule; its Linux cross-compile toolchain and custom ports are used as they are;
  - a static-CRT vcpkg triplet for the Windows cross build, chainloading that toolchain;
  - `scripts/` for setup, build, test, format and the runtime-dependency check;
  - `modules/stats/CMakeLists.txt`, `.clang-format`, `.gitignore`, `LICENSE`;
  - `README.md` (setup, build and test commands only);
  - the "To fill in" section of `CLAUDE.md`, replaced by a link to the README section.
- **Interfaces:**
  - Targets: `tom_stats_core` (static library, standard C++ plus nlohmann/json and SimpleIni), `tom_stats_core_tests` (Catch2), `tom_stats_plugin` (Windows DLL, only in the cross preset).
  - Presets: `core` (native Linux, GCC or Clang) and `plugin` (clang-cl, lld-link, ninja).
- **Verification:**
  - `unit`: `cmake --preset core && cmake --build --preset core && ctest --preset core` passes with one trivial test, and `ctest --preset core -R <name>` runs a single test.
  - `scripts/build.sh` runs the unit tests and produces an empty DLL that links against CommonLibSSE-NG and does not import the C++ runtime DLLs.
- **Depends on:** none.

#### T1.2 Plugin skeleton loads in game; verify game version

- **Goal:** the DLL loads under SKSE and logs its version and the game version.
- **Covers:** [§8 Constraints](PRD.md#8-constraints) (SKSE plugin, no ESP), assumption 10.
- **Scope:** `modules/stats/plugin/` (entry point, logging setup).
- **Interfaces:** SKSE plugin version data. The SKSE messaging listener (`kPostLoad`, `kDataLoaded`, `kPreLoadGame`, `kPostLoadGame`, `kNewGame`) is the single place where later tasks hook in.
- **Verification:**
  - `log`: the game starts, and the log contains the plugin version, the runtime version and one line per received SKSE message.
  - Also confirm and record the result in the Risks section:
    - the installed game version (expected 1.7.104);
    - that an Address Library for that version exists;
    - that the pinned CommonLibSSE-NG version supports it.
  - If the Address Library or CommonLibSSE-NG does not support 1.7.104, stop and report.
- **Depends on:** T1.1. Runs in parallel with Phase 2.

#### T1.3 Unit tests under ASan and UBSan in CI

- **Goal:** CI runs the native unit tests under AddressSanitizer and UndefinedBehaviorSanitizer on every pull request and push to `main`, so a memory or undefined-behaviour error fails CI instead of passing by chance (user decision, 2026-11-26). PR #37's review found a dangling temporary (range-for over `.items()` of a temporary `nlohmann::ordered_json`) that GCC 13 in CI (no P2718R0) produces and local GCC 15 hides; CI had no sanitizer to catch it.
- **Covers:** success criterion 4 (infrastructure). No PRD requirement changes.
- **Scope:**
  - `CMakePresets.json`: a `core-asan` configure, build and test preset, and a `core-asan` workflow preset;
  - `.github/workflows/ci.yml`: a new job;
  - `CONTRIBUTING.md#ci`: one bullet for the new job (the list of what CI runs lives there);
  - `README.md#build-and-test`: one command line for the local run, next to the `scripts/test.sh` lines; the flags stay in `CMakePresets.json`, the README only names the preset.
  - Not touched: `scripts/test.sh` (it stays on `core`), `lib/skyrim-mod-toolchain`, the module code. The toolchain submodule owns the setup action and the `skse-clangcl` preset only; the `core` preset, the workflow and the sanitizer flags are this project's, so nothing here belongs in the submodule.
- **Interfaces:**
  - Configure preset `core-asan`: inherits `core` (so `binaryDir` is `build/core-asan`, and `build/core`, which `lint.sh` reads, is left alone). Adds cache variables `CMAKE_CXX_FLAGS`, `CMAKE_EXE_LINKER_FLAGS` and `CMAKE_SHARED_LINKER_FLAGS`, each `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`. No `CMAKE_C_FLAGS`: the project enables only CXX. These are the flags the verifiers have used by hand.
  - Build preset `core-asan` (configure preset `core-asan`).
  - Test preset `core-asan`: the `core` test preset's output settings, plus `environment` `ASAN_OPTIONS=halt_on_error=1:detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
  - Workflow preset `core-asan` (presets version 6, which the file already uses, has workflow presets): configure, build and test steps of `core-asan`. The one command, locally and in CI: `cmake --workflow --preset core-asan`. A single test runs with `ctest --preset core-asan -R "<test name>"` after it.
  - CI job `sanitizers`, named "Unit tests (ASan, UBSan)": `runs-on: ubuntu-24.04` like `checks` (its default `c++` is GCC 13.3), checkout with `submodules: recursive`, the toolchain setup action with `native-tests: "true"` (it provides `VCPKG_ROOT` and the vcpkg binary cache), then `cmake --workflow --preset core-asan`. A separate job with `needs: checks` (user decision, 2026-11-27), so it starts after `checks` and does not race it for the vcpkg cache key (see Risks). Triggers and concurrency are the workflow's, unchanged.
- **Verification:**
  - `cmake --workflow --preset core-asan` passes on the branch locally, with all unit tests run (370 at the time of planning), and `scripts/test.sh`, the format check and lint still pass.
  - The tests inherit `ASAN_OPTIONS` and `UBSAN_OPTIONS` from the test preset's `environment`, which applies to the ctest process, not per test, so `ctest --show-only=json-v1` does not list it: a throwaway test that checks `getenv`, or a UBSan seed that prints a stack trace only under `ctest --preset core-asan`, shows it.
  - Seeded errors, each in a throwaway test case that is not committed to the branch: a heap buffer overflow (read one past a `std::vector`'s end through `data()`) and a signed `int` overflow each make `cmake --workflow --preset core-asan` exit non-zero, with an `AddressSanitizer` or `runtime error` report in the output, while `scripts/test.sh` passes with the same seeds.
  - CI: the pull request's run shows the `sanitizers` job on `ubuntu-24.04`, its log shows GCC 13.3 (`The CXX compiler identification is GNU 13.3`), and it passes. On a draft pull request that is closed unmerged afterwards, the job fails for each seed above and for the PR #37 pattern (range-for over `.items()` of a temporary `nlohmann::ordered_json`), which shows as a `stack-use-after-scope` under GCC 13 (`heap-use-after-free` is the variant over a member of a temporary); the implementer links those runs in the task's pull request.
  - `actionlint .github/workflows/ci.yml`, if installed; otherwise report it as not run.
- **Depends on:** T1.1. Shares no files with the other open tasks; **parallel** with all of them.
- **Status:** merged (#48), with `needs: checks`. Draft PR #47 (closed unmerged) showed the job failing for all four seeds (runs 45910266829 and 45911079364).

### Phase 2: Core library (game-independent, `modules/stats/core/`)

All Phase 2 tasks are checked by `unit` tests in `tom_stats_core_tests`. Expected values come from the PRD formulas and the [curves](../PRD.md#41-curves), computed by hand in the test.

#### C1 Curves

- **Goal:** evaluate the three curves of the [overview §4.1](../PRD.md#41-curves) and parse them from JSON.
- **Covers:** ST-9, ST-10, ST-15 (formula base).
- **Interfaces:** `Curve` (variant of linear, power, asymptotic), `double evaluate(const Curve&, double x)`, `parseCurve(json) -> expected<Curve, Error>`.
- **Verification:** `unit` tests check that:
  - linear curves give the expected values;
  - power is mirrored for `x < 0` and is the square root for λ = 0.5;
  - asymptotic gives `S` at 0, approaches `E` and treats `c` as `|c|`;
  - asymptotic is mirrored through `(0, S)` for `x < 0`: bounded by `2S − E`, with the same slope on both sides of 0;
  - the ST-15 example values come out (weapon fit, trade curve);
  - unknown curve names and missing parameters are rejected.
- **Depends on:** T1.1. **Parallel** with C2, C4.

#### C2 JSON merge engine

- **Goal:** load one table folder by the [overview merge rules](../PRD.md#4-shared-constraints) and return the merged object plus diagnostics.
- **Covers:** ST-27, [§9 Broken patches](PRD.md#9-risks).
- **Interfaces:**
  - `PluginQuery` (abstract: active plugins in load order, with name and extension).
  - `TableSource` (file name plus text, and whether it is a mod patch from `plugins/`). A filesystem enumerator is a thin helper on top.
  - `mergeTable(sources, PluginQuery&, registered) -> MergeResult { json merged; vector<Diagnostic> diagnostics; map<pointer, Origin> origins }`, with the API-registered entries as the lowest layer (assumption 3). Diagnostics cover file order, conflicts, removed keys, warnings for same-name plugins and skipped invalid files.
  - Entry validation per file ([§9 Broken patches](PRD.md#9-risks)): `mergeTable` and `loadTableFolder` take an optional `EntryValidator`, a function `(entry name, entry after merging this file) -> optional<reason>` that C3 supplies per table. Each file is merged into a staged copy of all merge state: the merged JSON, the origins, the patch owners of each key (the `Merger`'s `owners_`) and the file's `Loaded`, `Conflict` and `Removed` diagnostics. The validator runs on every top-level entry the file sets and does not remove with `null`. If one entry fails, all of the staged state is dropped together, so the whole file is skipped as if it had never been read, and a new `Diagnostic::Kind::InvalidEntry` names the file, the entry's JSON pointer and the reason. Otherwise the staged state is committed. Because the validator sees the entry after the merge, a patch may set single fields of an existing entry, but an entry it adds must be complete. API-registered entries are checked the same way when they are registered (A1), not here.
  - The merge engine already exists (`modules/stats/core/include/tom/stats/core/table_merge.h`). The validator is an amendment to it with a default of "accept all", so current callers and tests stay unchanged.
- **Verification:** `unit`, one test per rule in the overview, including:
  - same-name plugins: the file loads at the last of them and a warning is logged;
  - overriding the default file is not reported as a conflict;
  - an invalid file is skipped and the remaining files still load;
  - a file with one invalid entry and one valid entry is skipped as a whole, with an `InvalidEntry` diagnostic, and the files after it still load;
  - a patch that sets one valid field of an existing entry loads; a patch that adds an incomplete entry, or removes a required field with `null`, is skipped;
  - a file after a skipped file that sets the same key reports no conflict and gets that key's origin; the skipped file leaves no `Loaded`, `Conflict` or `Removed` diagnostic.
- **Depends on:** T1.1. **Parallel** with C1, C4.

#### C3 Settings and tables

- **Goal:** typed configuration from the INI and the four merged tables, the shipped default files and the JSON schemas.
- **Covers:** ST-3 (default base, race adjustments), ST-6 (`r` validation at load), ST-9, ST-14 (mapping data), ST-15 (check and training data), ST-21 (groups), ST-24 (default key and button), ST-27, ST-28 (incompatible list).
- **Scope:** `core/config/`, `modules/stats/data/trials-of-mastery-stats.ini`, `data/{stats,skills,actions,incompatible}/_default.json`, `data/schema/{stats,skills,actions,incompatible}.schema.json`.
- **Interfaces:**
  - `Settings`: the INI values of ST-27, plus the gamepad button of ST-24. The shipped INI sets the stat box key to K and the gamepad button to Y (D7); S2 checks that both are free in the skills menu ([S2 result](#s2-result): K is, Y is checked by hand in U1).
  - `StatsTable`: per stat, the group, the default base (10 for every core stat, confirmed, ST-3), the race adjustments (ST-3, ST-27) and the effects (actor-value or named, each with a curve). Race adjustments are an object keyed by the race's editor ID (ST-27, `"NordRace": 1`); `RE::TESRace` keeps its editor ID at runtime. Races without an entry get no adjustment. `buildTables` takes the set of race editor IDs the loaded plugins define (empty in unit tests unless given); an adjustment for any other race is ignored and logged at debug level only ([§9](PRD.md#9-risks)), and the file still loads. The shipped default has no race adjustments: the PRD names none.
  - `SkillsTable`: per skill, the primary stat, secondary stat and split.
  - `ActionsTable`: per action, the check inputs with curves, the output mapping (assumption 7) and the training curves per stat.
  - `IncompatibleTable`: per entry, the game plugin names and SKSE plugin names to detect.
  - One `EntryValidator` per table (C2) that checks an entry's fields, types, curves and value ranges (for example, integer bases). Names added by other mods are checked against the namespacing rule of ST-27 here as well. These validators are where invalid entries are found; `buildTables` gets only merges whose files passed them.
  - The validators check an entry in itself only. Whether a stat it names exists is not their concern; `buildTables` handles that.
  - `loadSettings(text)` and `buildTables(MergeResult...) -> expected<Tables, Errors>`. `buildTables` converts the merged JSON to the typed tables. A skills or actions entry that names a stat no layer of the stats table defines is dropped on its own and logged with the files that set it (from `MergeResult::origins`, empty for an API registration); its file and the rest of the table still load ([§9](PRD.md#9-risks)).
- **Placeholders:**
  - Values: effect coefficients (OQ-2), the split ratio (OQ-3), learning-speed curves (OQ-4), check weights and output mappings (OQ-5), redirection share (OQ-6), direct activity XP (OQ-7), STR XP for unarmed attacks by glove weight (OQ-9), PER XP for fighting (OQ-10).
  - The incompatible default has one entry for each mod that [§7.9](PRD.md#79-compatibility) marks "Detected at load (ST-28)". The game plugin and SKSE plugin names of each entry live only in `incompatible/_default.json`. The names are complete and confirmed from the mods' archives, so the list carries no `TODO` marker.
  - Each placeholder is marked `TODO(OQ-n)` in a `"_todo"` field beside its key, since JSON has no comments.
- **Verification:** `unit` tests check that:
  - the shipped defaults parse and validate against the schemas;
  - they contain the default mapping and groups of [ST-27](PRD.md#78-configuration), a default base of 10 for every core stat, and one incompatible entry for each mod §7.9 marks "Detected at load";
  - a race adjustment patch for one race leaves the other races without an adjustment, and a non-integer adjustment skips the file;
  - an adjustment for a race missing from the given race set is ignored with a debug-level log entry only, and the rest of its file loads;
  - each validator rejects a wrong type, a missing required field, an unknown curve and a non-namespaced mod name, and a file with such an entry is skipped as a whole with file and key in the diagnostic ([§9](PRD.md#9-risks));
  - a skills or actions entry that names an undefined stat (never defined, or removed by a later stats patch, or from an API registration) is dropped and logged with its files, while the other entries of its file and the rest of the table load;
  - INI defaults are loaded when keys are missing;
  - an INI value of `r` that [ST-6](PRD.md#72-xp-and-cost) rejects is logged and replaced by the built-in default, the same one used for a missing key (at load, that default is the previous value).
- **Depends on:** C1, C2. **Sequential.**
- **Status:** merged (#12) before PR #31; C3a adds the display name, the short name, the namespace group and the group order.

#### C3a Display names, namespace groups and group order in the stats table (C3 amendment)

- **Goal:** bring the merged C3 code, the stats schema and the default stats file in line with PR #31: a stat entry carries a display name and a short name, a registered stat without a group is in its namespace's group, and the tables keep the order in which the stats files name the groups.
- **Covers:** ST-21 (namespace groups, group order), ST-24 (display name and short name), ST-27 (stats-file fields), [§7.8](PRD.md#78-configuration) default groups.
- **Scope:**
  - `modules/stats/core/include/tom/stats/core/table_merge.h`, `modules/stats/core/src/table_merge.cpp` (the key order only);
  - `modules/stats/core/include/tom/stats/core/config/tables.h`, `modules/stats/core/src/config/tables.cpp`;
  - `modules/stats/data/schema/stats.schema.json`, `modules/stats/data/stats/_default.json`;
  - `modules/stats/tests/unit/table_merge_test.cpp`, `tables_test.cpp`, `default_tables_test.cpp`.
- **Interfaces:**
  - `StatEntry` gains `std::optional<std::string> displayName` and `std::optional<std::string> shortName`, read from the fields `displayName` and `shortName` (strings of at least one character), which the schema, the stats validator and `kStatKeys` gain. A registered stat brings both in its stats-table entry, as it brings its group and base (A1); there is no separate API call.
  - A stat entry without `group`: a namespaced stat is in the group its namespace names (`Luck:LUC` in `Luck`, [ST-21](PRD.md#75-level-up)), any other stat in `kOtherGroup`. A new `[[nodiscard]] std::string_view namespaceOf(std::string_view name)` in `tables.h`, next to `isNamespaced`, returns the part before the colon, or an empty view for a name that is not namespaced. The comments on `kOtherGroup` and `StatEntry` and the schema's `group` description say so.
  - `MergeResult` gains `std::vector<std::string> keyOrder`: the top-level keys of `merged` that a file set, in the order the files first set them, files in merge order and each file's keys in its text order. Keys that only the registered layer sets are not in it. A key that a `null` removes leaves the list; a later file that adds it again appends it. A skipped file adds nothing, since its staged state is dropped (C2). `nlohmann::json` sorts object keys, so the text order has to be read separately, for example by parsing the file a second time as `nlohmann::ordered_json`.
  - `Tables` gains `std::vector<std::string> statGroups`: each group that the stats files set (the origin of `/<stat>/group` is not `Layer::Registered`), once, at the `keyOrder` position of its first stat (assumption 17). `buildTables` fills it; C9b orders the sections by it.
  - `_default.json`: `displayName` and `shortName` for the eight core stats, as the [PRD §6](PRD.md#6-terminology) table names them (`Strength` and `STR`, and so on). Its entries are reordered to STR, CON, END, INT, WIS, DEX, PER, CHA, so `statGroups` is Warrior, Mage, Thief, Other (assumption 17).
- **Verification:** `unit` tests check that:
  - the shipped default gives each core stat its display name and short name, and `statGroups` is Warrior, Mage, Thief, Other;
  - an entry without `displayName` or `shortName` loads with them empty; a non-string or empty one skips the file with an `InvalidEntry` diagnostic (C2);
  - `Mod:Foo` without a group is in `Mod`, with group `Mage` in Mage, and a core stat whose group a patch removes with `null` is in Other;
  - `namespaceOf("Luck:LUC")` is `Luck`, and `namespaceOf("STR")` is empty;
  - `keyOrder` follows each file's text order, not the alphabet; a patch's new key is appended; a key removed with `null` leaves the list and comes back at the end when a later file adds it again; a skipped file adds no key; a key only the registered layer sets is absent;
  - `statGroups` lists each group once; a patch that gives an existing stat a new group puts that group at the stat's position; a group that only a registered entry sets is not in it;
  - `scripts/test.sh`, the format check and lint pass ([CONTRIBUTING.md#workflow](../../CONTRIBUTING.md#workflow)).
- **Depends on:** C3. **Parallel** with C7a, C8 and C9a (no shared files).

#### C3b Stat box lines per page in the INI (C3 amendment)

- **Goal:** the INI setting of the lines per stat box page (ST-24, ST-27, PR #33).
- **Covers:** ST-24 (lines per page), ST-27.
- **Scope:** `modules/stats/core/include/tom/stats/core/config/settings.h`, `modules/stats/core/src/config/settings.cpp`, `modules/stats/data/trials-of-mastery-stats.ini`, `modules/stats/tests/unit/settings_test.cpp`.
- **Interfaces:** `Settings` gains `std::int32_t statBoxLinesPerPage`, read from `[StatBox] LinesPerPage`. A value below 2 (a page needs a heading and a line, assumption 17) or not an integer is rejected and reported like the other keys, and the default stays. The default is a placeholder until S2 measures it, and U1 sets the measured value in `settings.h` and the shipped INI; the INI comment says so. The placeholder is 15, the line count of the core stats' overview. S2 keeps 15 ([S2 result](#s2-result)); U1 drops the placeholder comments.
- **Verification:** `unit` tests check that a missing key gives the default, a valid value is read, 1, 0, a negative and a non-integer value are rejected with the key in the error, and the shipped INI still loads without errors and matches the built-in defaults; `scripts/test.sh`, the format check and lint pass.
- **Depends on:** C3. **Parallel** with every other task except C3c, which changes the same files after it.

#### C3c Dismissed compatibility warnings in the INI (C3 amendment)

- **Goal:** the INI list of the mods whose ST-28 warning was dismissed, and the rule on incompatible entry names that keeps that list unambiguous (PR #43, assumption 18).
- **Covers:** ST-27 (the dismissed mods), ST-28 (dismissal per mod).
- **Scope:**
  - `modules/stats/core/include/tom/stats/core/config/settings.h`, `modules/stats/core/src/config/settings.cpp`, `modules/stats/data/trials-of-mastery-stats.ini`, `modules/stats/tests/unit/settings_test.cpp`;
  - `modules/stats/core/src/config/tables.cpp` (the incompatible validator only), `modules/stats/data/schema/incompatible.schema.json`, `modules/stats/tests/unit/tables_test.cpp`.
- **Interfaces:**
  - `Settings` gains `std::vector<std::string> dismissedIncompatibleMods`, read from `[Compatibility] DismissedWarnings` by the rule of assumption 18. Every text is a valid list, so the key adds no errors; a missing key gives an empty list.
  - Two free functions in `settings.h`, so the read and the write-back (C10, K1) use one format:

    ```cpp
    // Splits a comma-separated list: items trimmed of spaces and tabs, empty
    // items left out, a repeated item kept once, at its first position.
    [[nodiscard]] std::vector<std::string> parseNameList(std::string_view text);
    // Joins the items with ", ".
    [[nodiscard]] std::string formatNameList(std::span<const std::string> names);
    ```

  - The incompatible validator (`entryValidator(Table::Incompatible)`) rejects an entry whose name is empty, contains a comma or a control character, or starts or ends with whitespace; the schema gets the same rule as `propertyNames` with a `pattern`. The names of the shipped default pass.
  - The shipped INI loses the `TODO(OQ-8)` comment line on `AllowIncompatibleMods` (OQ-8 is closed) and gets `DismissedWarnings =` (empty) in `[Compatibility]`, with a comment: the names are those of the incompatible file, separated by commas; the "Don't show again" button adds to the list; removing a name shows its warning again; a listed mod still disables the module.
- **Verification:** `unit` tests check that:
  - a missing key and an empty value give an empty list, and the shipped INI still loads without errors and matches the built-in defaults; `grep -rn 'OQ-8' modules/stats` finds nothing;
  - `Requiem, Valhalla Combat` gives both names; ` Requiem ,,Valhalla Combat, ` gives the same two; `Requiem, Requiem` gives one; a name with inner spaces (`Skyrim Attributes Mod.`) is kept whole;
  - `formatNameList` of the two names gives `Requiem, Valhalla Combat`, and `parseNameList(formatNameList(x)) == x` for a list of valid entry names;
  - an incompatible patch with an entry named `A, B`, ` Requiem`, `Requiem ` or with a newline is skipped as a whole with an `InvalidEntry` diagnostic (C2), and the shipped default validates against the changed schema;
  - `scripts/test.sh`, the format check, lint and the sanitizer run pass ([CONTRIBUTING.md#workflow](../../CONTRIBUTING.md#workflow)).
- **Depends on:** C3 (and C3b, merged, which last changed `settings.h`). **Parallel** with every open task: no other open task changes these files.

#### C4 Stat values, cost and progression

- **Goal:** per-stat and per-actor state with cost, XP gain with carry-over, advance, ascend and effective value.
- **Covers:** ST-1, ST-2 (registry, free-point opt-out), ST-3 (base and default base), ST-4, ST-5, ST-6 (math), ST-7, ST-8.
- **Interfaces:**
  - `StatId`. `StatRegistry` holds the core stats plus registered ones, with a dormant flag (assumption 8) and a flag for registered stats opted out of free level-up points ([ST-2](PRD.md#71-stats), for example LUC, [LU-1](../luck/PRD.md)). `registerStat` (already implemented) gains that flag as a parameter, defaulting to "gets free points"; core stats always get them.
  - `StatValue { int32 base, accumulated, active; double xp; }`.
  - `ActorStats`: stat values, multiplier, `map<key,int32>` modifiers per stat and `map<key,int32>` base adjustments per stat (assumption 11).
  - `initStat(ActorStats&, StatId, int32 defaultBase)`: the base becomes the default base plus the stat's adjustments (ST-3).
  - `setBaseAdjustment(ActorStats&, StatId, key, int32)` and `removeBaseAdjustment(ActorStats&, StatId, key)`: change the base by the difference (ST-3).
  - `ActorStats` also stores the default base last applied per stat; `initStat` sets it. `applyDefaultBase(ActorStats&, StatId, int32 defaultBase)` moves the base by the difference to it and stores the new value (ST-3). The stored value is the reference of the relative effects (assumption 12, C5).
  - `double cost(double r, int32 k)` returns `+inf` on overflow.
  - `GainResult addXp(StatValue&, double xp, double r)`.
  - `advance(StatValue&)`.
  - `ascend(StatValue&, function<double(int32)>)`; the module rounds the formula's result down (ST-6).
  - `double effective(const ActorStats&, StatId)`.
- **Verification:** `unit` tests for the cost formula and overflow, several points from one large XP amount with carry-over, advance, ascend with rounding down (2.9 gives 2), effective value with several modifier keys and removal of one key, and no clamp at 100. For base adjustments ([ST-3](PRD.md#71-stats)):
  - a race +1 and a class −2 on a default base of 10 give a base of 9;
  - adding, changing and removing an adjustment after creation moves the base by the adjustment's difference and leaves the stored default base alone;
  - after an ascend, removing an adjustment keeps the ascended points in the base;
  - `applyDefaultBase` from 10 to 12 moves the base by +2 and stores 12; applying 12 again changes nothing.
- **Depends on:** T1.1. **Parallel** with C1, C2. `StatValue`, `ActorStats` and `StatRegistry` exist already; the base adjustments are an amendment. The start value this task first stored is removed again by C5a.

#### C5 Effect math

- **Goal:** compute effect values and the per-actor-value deltas that the game layer applies.
- **Covers:** ST-9 (named effects on any stat, curve input), ST-10, ST-11, ST-12, ST-13 (math).
- **Interfaces:**
  - `avContribution(curve, effective, defaultBase)` per ST-10, with the default base last applied as the reference (assumption 12).
  - `ContributionLedger`: the last contribution per actor and actor value. `vector<AvDelta> updateContributions(ledger, actor, tables)`.
  - `RedirectSplit splitDamage(damage, share, poolAvailable) -> { fromPool, toHealth }`.
  - `double learningBonus(actor, statsTable)`: the `LearningSpeed` terms of every stat that lists the effect (ST-9), summed and then clamped to the ST-12 floor and cap. The clamp applies to the stat bonus only. Outside bonuses are applied by G6, not here.
  - `double baseCritChance(actor, statsTable, kind)`: the terms of every stat that lists the crit effect of that kind (ST-13).
  - `CritShiftRegistry` with `set(owner, CritShift)` and `remove(owner)`, where `CritShift = function<double(actor, kind, baseChance)>` returns a shift; the actor lets the luck module read LUC (LU-7). `double critChance(actor, statsTable, registry, kind)` is `baseCritChance` plus the sum of all shifts.
  - The named effects' curve input (ST-9): an optional `input` field per named effect in the stats file, `"relative"` (`curve(x) − curve(0)` with `x = effective − default base`, the default) or `"absolute"` (`curve(effective)`). This amends C3's schema, reader and `Effect`. The END share for `splitDamage` is the `DamageRedirection` term computed the same way.
- **Verification:** `unit` tests check that:
  - a new character without adjustments (effective = default base) gets a zero contribution, and one with a race +2 gets `curve(2) − curve(0)`;
  - a stat change yields only the difference;
  - ascending shrinks the contributions;
  - the damage split is correct with an empty pool, a short pool and a full pool;
  - a curve set in the file to exceed the cap still yields at most the ST-12 cap, and a malus stops at −100 %;
  - a named effect with `input` absolute gives `curve(effective)`, and one with relative (or without `input`) gives `curve(effective − default base) − curve(0)`;
  - a named effect on a stat other than the default ones (for example a registered stat) adds to the sum;
  - crit shifts from two owners add up and receive the current base crit chance and the kind, and removing an owner removes its shift;
  - a stats file with an `input` value other than `relative` or `absolute`, or with `input` on an actor-value effect, is skipped as invalid (C2, C3).
- **Depends on:** C1, C3, C4. Placeholders depend on OQ-2, OQ-4, OQ-6.
- **Status:** merged with the start value as the reference; C5a brings it in line with the current ST-3, ST-9, ST-10 and ST-13.

#### C5a Effects measured from the default base (C4/C5 amendment)

- **Goal:** bring the merged C4 and C5 code, the stats schema, the default stats file and their tests in line with the PRD change of PR #23: relative effects measure from the default base, not from a start value, and the default crit effects are absolute.
- **Covers:** ST-3 (adjustments change effects; a default-base patch does not), ST-9 (relative and absolute named effects), ST-10, ST-13 (absolute default crit), [§8](PRD.md#8-constraints) (no start value in the save).
- **Scope:**
  - `modules/stats/core/include/tom/stats/core/actor_stats.h`, `modules/stats/core/src/actor_stats.cpp`;
  - `modules/stats/core/include/tom/stats/core/effects.h`, `modules/stats/core/src/effects.cpp`;
  - `modules/stats/core/include/tom/stats/core/config/tables.h` (the comment on `CurveInput::Relative` only);
  - `modules/stats/data/schema/stats.schema.json` (the `input` description only);
  - `modules/stats/data/stats/_default.json` (`"input": "absolute"` on DEX's `CritChancePhysical` and `CritChanceMagic`; the curves and their `TODO(OQ-2)` stay as they are. Taken as absolute, the placeholder asymptotic curve gives DEX 10 a crit chance of about 4.76 instead of 0; whether that is wanted is part of OQ-2, not of this task);
  - `modules/stats/tests/unit/actor_stats_test.cpp`, `effects_test.cpp`, `default_tables_test.cpp`.
- **Interfaces:**
  - `ActorStats::startValues` is removed. `initStat`, `setBaseAdjustment`, `removeBaseAdjustment` and `applyDefaultBase` keep their signatures and move only the base; `initStat` and `applyDefaultBase` still store the default base last applied in `ActorStats::defaultBases`.
  - `avContribution(const Curve&, double effective, double defaultBase)`: same shape, the third parameter is the default base (`curve(effective − defaultBase) − curve(0)`).
  - `updateContributions` and `namedEffectValue` read the reference from `ActorStats::defaultBases` (missing counts as 0, assumption 12). A relative named effect adds `curve(x) − curve(0)`, as an actor-value effect does; an absolute one adds `curve(effective)`.
  - `CurveInput` keeps its two values; only its comment and the schema description change from "start value" to "default base".
- **Verification:** `unit` tests check that:
  - an actor with `initStat(10)` and no adjustments gets no contribution and a relative named effect of 0, also for a curve with `curve(0) ≠ 0` (`b`, `S`);
  - a race adjustment of +2 set before `initStat(10)` gives an actor-value contribution and a relative named effect of `curve(2) − curve(0)` at creation;
  - adding a +2 adjustment after creation yields an `AvDelta` of `curve(2) − curve(0)` from `updateContributions`, and removing it yields the negative of that;
  - `applyDefaultBase` from 10 to 12 at multiplier 1 moves the base by +2 and leaves `updateContributions` with no delta and relative named effects unchanged;
  - a relative named effect with a curve that is not 0 at 0 subtracts `curve(0)`;
  - the shipped DEX crit effects parse as `CurveInput::Absolute`, and `baseCritChance` at DEX 12 is `curve(12)` with no reference subtracted;
  - `ActorStats` has no start value (the tests compile without `startValues`), so C8 has nothing of it to persist;
  - `scripts/test.sh`, the format check and lint pass ([CONTRIBUTING.md#workflow](../../CONTRIBUTING.md#workflow)).
- **Depends on:** C4, C5. **Must merge before C7.**

#### C5b END's small effects and the sneak reveal in the core (C5 amendment, PR #55)

- **Goal:** add the named effects, the default effect entries and the spell crit multiplier that PR #55 brings into ST-11, ST-13 and ST-13a, so the game tasks G7a, G7b, G8 and G11 can read their values from the service.
- **Covers:** ST-9, ST-10 (END's resistance to poison), ST-11 (fall damage mitigation, breath duration), ST-13 (spell crit multiplier), ST-13a (sneak reveal range).
- **Scope:**
  - `modules/stats/core/include/tom/stats/core/config/tables.h` and `core/src/config/tables.cpp`: three new `NamedEffect` values and their names;
  - `modules/stats/core/include/tom/stats/core/effects.h` and `core/src/effects.cpp`: one value function each, as `damageRedirectionShare` has;
  - `modules/stats/core/include/tom/stats/core/stats_service.h` and `core/src/stats_service.cpp`: one accessor each;
  - `modules/stats/data/schema/stats.schema.json`: the three names in the `named` enum;
  - `modules/stats/data/stats/_default.json`: END gets an actor-value effect on `PoisonResist` next to CON's (the values add up, ST-9) and the two named effects; PER gets the sneak reveal; the spell crit multiplier is added in the form ST-13 gives it, with a placeholder marked `TODO(OQ-2)`;
  - the unit tests of the tables, effects and service.
- **Interfaces:**
  - `NamedEffect::FallDamageMitigation`, `NamedEffect::BreathDuration`, `NamedEffect::SneakReveal`, named `FallDamageMitigation`, `BreathDuration` and `SneakReveal` in the stats file.
  - `fallDamageMitigation(actor, stats)`, `breathDuration(actor, stats)` and `sneakRevealRange(actor, stats)` in `effects.h`, each the summed `namedEffectValue` (C5).
  - `StatsService::fallDamageMitigation(ActorId)`, `breathDuration(ActorId)` and `sneakRevealRange(ActorId)`, each `std::expected<double, Error>` like `damageRedirectionShare`, and a read of the spell crit multiplier for G8.
  - The default entries are relative, the PRD's default for named effects (ST-9), with placeholder curves marked `TODO(OQ-11)` (END) and `TODO(OQ-12)` (PER). What each value means in the game (a share of the fall damage taken off, a breath factor or seconds, a range in game units) is part of those questions: C5b does not pick it silently, each placeholder's `TODO` names the unit it assumes, and G7a, G7b and G11 raise it with the user before they start.
- **Verification:** `unit`: the three names parse and an unknown name is still rejected; the schema accepts them; the default tables load with the new entries and log their `TODO(OQ-11)` and `TODO(OQ-12)` markers; each accessor returns the curve value for an actor and fails for an unknown one; END's and CON's `PoisonResist` contributions add up in the ledger.
- **Depends on:** C5, C7. It shares the service files with C7a and C8, so it runs after them. Placeholder values depend on OQ-2, OQ-11 and OQ-12.

#### C6 Training, checks and XP modifiers

- **Goal:** compute the stat XP from skill level-ups and actions, and check values.
- **Covers:** ST-14, ST-15 (math), ST-16 (levels above 100, registered mappings and actions), ST-17.
- **Interfaces:**
  - `XpModifierRegistry`, keyed by owner, per skill for skill XP, per skill for stat XP on level-up, and per action (assumption 4).
  - `skillLevelUpXp(skill, levelReached) -> vector<{StatId, double}>`.
  - `actionTrainingXp(action, input) -> vector<{StatId, double}>`.
  - `double checkValue(action, actor, skillLevels)`.
  - `double checkResult(action, I)` (assumption 7).
  - API entries reach the tables through C2's `registered` layer (assumption 3).
- **Verification:** `unit` tests check that:
  - the split applies to the level reached;
  - an unmapped skill gives no XP;
  - a skill at level 150 trains with the same rule;
  - modifiers from two owners combine, and a modifier below −100 % is rejected (ST-17);
  - checks sum the stat terms and the skill terms;
  - the precedence follows assumption 3.
- **Depends on:** C1, C3, C4. Placeholders depend on OQ-3, OQ-5, OQ-7.

#### C7 Stats service, level-up logic and events

- **Goal:** one facade over registry, actor states, tables, modifiers and events. The game layer and the API call only this facade.
- **Covers:** ST-2 (free-point opt-out), ST-3 (adjustments), ST-13 (crit shift registration), ST-6 (auto-advance, advance claim, N, `setR` validation), ST-18, ST-20, ST-21 (opt-out), ST-22 (claim flag), ST-26 (operations and events).
- **Interfaces:**
  - `StatsService` operations:
    - read values; `addXp`; `setValues`;
    - `setModifier` and `removeModifier`; `setMultiplier`;
    - `setBaseAdjustment(actor, stat, key, value)` and `removeBaseAdjustment(actor, stat, key)` (ST-3, ST-26); the module's race key is rejected; the returned `AvDelta`s carry the effect change of the adjustment's difference (ST-3, ST-10);
    - `setR` (one value per save, player only, see ST-6 and [§5](PRD.md#5-scope)); `setN`;
    - `claimAdvancing(owner)`; `advance` and `advanceAll`; `ascend`;
    - `onPlayerLevelUp(newLevel)`: exactly one real level-up, the game's own level-up flow (G5). It grants the free points of one level and advances when `newLevel` is a multiple of N (ST-6). It takes no previous level and computes no level difference, so a level change that bypassed the level-up (`setlevel` and the like) is never counted, neither as points nor as advancing (ST-18). G5 is its only caller;
    - `setCritShift(owner, CritShift)` and `removeCritShift(owner)` (ST-13, ST-26), over C5's `CritShiftRegistry`. `CritShiftRegistry::sum` calls the owners' functions while it iterates its map, so a shift that calls `setCritShift` or `removeCritShift` would invalidate the iteration: the service calls the shifts on a snapshot of the registry, or rejects `set` and `remove` while a sum runs;
    - `claimPointAssignment(owner)`; `assignFreePoint(stat)`, which rejects a stat opted out of free points (ST-2, ST-20), also for the module that claimed point assignment, and a dormant stat: `StatRegistry::getsFreePoints` is true for a dormant stat that has not opted out, so the service checks `isDormant` as well (assumption 8).
  - Effects: the service passes only the active stats (`StatRegistry::activeStats`, not dormant ones) to `updateContributions`, `namedEffectValue` and the functions built on it (assumption 8).
  - A stat gets a base only through `initStat`, also for a stat registered mid-game and in `setValues`, so its default base last applied is set (assumption 12); without it the whole base would count as above the reference.
  - `EventSink` interface with the five ST-26 events.
  - A stat change returns the `AvDelta`s from C5.
- **Verification:** `unit` tests check that:
  - free points from a level-up go to accumulated and do not change the cost;
  - several real level-ups in a row, one `onPlayerLevelUp` each, grant the points once per call;
  - after a bypassed jump (the last call was for level 4, the next is for level 10), the call grants the points of one level only, and advances only because 10 is a multiple of N; a bypassed change to a multiple of N, with no call, advances nothing (ST-18);
  - auto-advance follows ST-6 and stops after a claim;
  - `setN` changes the advance interval;
  - `setR` with a value that [ST-6](PRD.md#72-xp-and-cost) rejects keeps the previous value and logs the rejection;
  - a base adjustment of +2 added through the service yields the pool `AvDelta`s of `curve(2) − curve(0)`, and removing it yields their negatives; the race key is rejected;
  - a crit shift that removes itself or sets another shift during a call neither crashes nor changes the sum of that call;
  - `assignFreePoint` on a dormant stat is rejected, and a dormant stat's effects are left out of the pools;
  - a stat registered mid-game starts with its default base applied, and its relative effects are 0 at that base;
  - events fire in the right order with the right payloads;
  - unspent points stay pending;
  - `assignFreePoint` on an opted-out registered stat is rejected and leaves the points pending; the stat still gains XP and points from training.
- **Depends on:** C4, C5, C5a, C6.
- **Not in C7:** the XP modifiers, training and check operations of C6 (C7a), the read and restore accessors for persistence and `applyDefaultBases` (C8). The service builds on fixed tables (assumption 14) and is not thread-safe (assumption 15).

#### C7a Training, checks and XP modifiers in the stats service (C7 amendment)

- **Goal:** put the C6 functions behind `StatsService`, so the game layer and the API reach them only through the facade, as C7 and A1 require.
- **Covers:** ST-14, ST-15 (training and checks), ST-16 (registered mappings and actions), ST-17.
- **Scope:** `modules/stats/core/include/tom/stats/core/stats_service.h`, `modules/stats/core/src/stats_service.cpp` and the service's unit tests.
- **Interfaces** (`StatsService`; names may follow the existing style):
  - The service owns an `XpModifierRegistry`. `setXpModifier(owner, XpModifierKind, name, m) -> bool` and `removeXpModifier(owner, XpModifierKind, name)` pass on to it; `set` rejects what `XpModifierRegistry::set` rejects (ST-17).
  - `skillXpFactor(skill) -> double`: the `XpModifierKind::SkillXp` factor, which G6 multiplies with `learningBonus` and the game's own bonuses (ST-12, ST-17).
  - `trainSkillLevelUp(skill, levelReached) -> expected<vector<AvDelta>, Error>`: `skillLevelUpXp` with the service's tables and modifiers, then `addXp` to the player for each entry, with the `xpGained` and `pointGained` events of `addXp`. An unmapped skill gives no XP and no error (ST-16). G4 calls it.
  - `applyTraining(action, input) -> expected<vector<AvDelta>, Error>`: the same over `actionTrainingXp` (ST-15). G9a to G9c and the API's mod actions (G9d) call it. It fails for an action the actions table does not have.
  - `checkValue(actor, action, SkillLevels) -> expected<double, Error>` and `checkResult(action, i) -> expected<double, Error>` over C6's functions, failing where those return `nullopt` and for an unknown actor. `checkValue` sees only the active stats: a dormant stat's term is evaluated as for a stat the actor does not have (assumption 8).
  - Amounts that `addXp` ignores (not positive or not finite) are left out without an error, as C6 documents.
- **Verification:** `unit` tests check that:
  - a skill level-up through the service adds the C6 split to the player's stats and sends the events of `addXp`; an unmapped skill changes nothing;
  - XP modifiers set through the service change the XP of the next level-up and action, and removing them restores it; a modifier below −100 % is rejected;
  - `skillXpFactor` is 1 without modifiers and the product of `1 + m` with two owners (assumption 4);
  - `applyTraining` on an unknown action fails and changes nothing;
  - `checkValue` and `checkResult` match the C6 functions for the player and an NPC, fail for an unknown action, an action without a check and an unknown actor, and leave a dormant stat's value out.
- **Depends on:** C7. Not parallel with C8 (both change `stats_service.h` and `.cpp`); either may go first. **Parallel** with C9.

#### C8 Persistence format

- **Goal:** serialize and deserialize the full service state to versioned bytes, independent of the SKSE co-save.
- **Covers:** [§8 persistence constraint](PRD.md#8-constraints), success criterion 3 (format).
- **Interfaces:** `serialize(const StatsService&) -> bytes`, `deserialize(bytes, ActorIdResolver&) -> expected<State>`. `ActorIdResolver` maps saved actor IDs to current ones (FormID remap) or drops them. The format has a version field.
  - `restore(StatsService&, State) -> AvDeltasByActor` puts a `State` into the service: `resetSaveState`, then `addDormantStat` for each saved registered stat that is not active, `setR`, `restorePendingPoints`, and `restoreActor` for each actor. A saved `r` that `setR` rejects keeps the value from the settings and is logged. N is not touched (assumption 16).
  - Read and restore accessors on `StatsService` (C7 has `restoreActor`, `pendingPoints`, `r` and `n`, but nothing to read the actors):
    - `actorIds() -> vector<ActorId>` and `actorSnapshot(ActorId) -> expected<ActorSnapshot, Error>`, where `ActorSnapshot` holds the actor's stored race editor ID, `ActorStats` and `ContributionLedger`, the same three that `restoreActor` takes;
    - `restorePendingPoints(int32)`, failing for a negative count;
    - `StatRegistry::registeredStats()`: the registered stats, dormant ones included, so dormant stats are saved again (assumption 8, [§8](PRD.md#8-constraints) "registered stats"); and a way to drop the dormant stats on a revert;
    - `resetSaveState()`, for G2's revert on a new game or load: forgets the actors, the pending points and the dormant stats, and resets `r` to the service's construction value. It keeps the session state: N as the INI or a claiming module set it, the claims (assumption 16), the active registered stats, crit shifts, XP modifiers (C7a) and the event sink.
  - `applyDefaultBases() -> AvDeltasByActor`: `applyDefaultBase` (C4) for every actor and every active stat, with the default base of the stats table, so a patch that changed a default base moves the bases by the difference (ST-3, assumption 11). G2 calls it after a load. C7 has no such operation.
  - N, the claims and the per-session registrations (crit shifts, XP modifiers) are not in the format (assumption 16).
- **Verification:** `unit` tests check that:
  - all persisted fields of §8 round-trip, including base adjustments per key, the default base last applied per actor and stat, and dormant stats; there is no start value to store (C5a);
  - a restored stat with a base always has its default base last applied restored with it, so its effects keep their reference (assumption 12);
  - unresolved actors are dropped and logged;
  - an older format version is either read or rejected explicitly;
  - `serialize`, `deserialize` and `restore` into a new service built from the same tables give the same `actorSnapshot` per actor, the same pending points, `r` and dormant stats, and no `AvDelta`s; N stays as the new service has it;
  - `resetSaveState` leaves no actor and no pending point, resets `r`, and keeps N, the active registered stats, crit shifts, XP modifiers and claims;
  - after a change of a stat's default base in the tables, `applyDefaultBases` moves each actor's base by the difference and leaves the relative effects unchanged at multiplier 1 (assumption 13).
- **Depends on:** C7. Not parallel with C7a (shared files).

#### C9 Stat box model

- **Goal:** a pure state machine for the stat-box pages and their texts. It has no UI.
- **Covers:** ST-2 (opt-out in the box), ST-21, ST-22, ST-24 (content and flow).
- **Interfaces** (as merged, `stat_box_model.h`):
  - `StatBoxModel(StatsService& service, const StatsTable& stats, std::size_t maxButtons)`; `Page page() const`; `PressResult press(std::size_t button)`.
  - `Page { std::string text; std::vector<Button> buttons; }`, `Button { std::string text; }`, `Closed {}`.
  - `PressResult { std::variant<Page, Closed> next; std::vector<AvDelta> deltas; std::optional<Error> error; }`: the deltas of the free point a press assigned, for G3, and why a press changed nothing, for the log.
  - The model reads every value from the service when it builds a page. Each opening of the box builds a new model.
- **Verification:** `unit` tests for every page transition in ST-21 and ST-22 and for group pages when the stats plus "Back" exceed `maxButtons`. A registered stat opted out of free points (ST-2) shows on the info page but not on the "Level Up" pages, and a group left without stats there is not shown (ST-21).
- **Depends on:** C7. **Parallel** with C8 and C7a.
- **Status:** merged (#29) before PR #31. It lists the stats without sections or labels, puts a stat without a stats-table entry in Other, orders the groups by their first stat and formats numbers its own way. C9a and C9b bring it in line.

#### C9a Number format of the stat box

- **Goal:** the number format of ST-24 as a pure function, for C9b.
- **Covers:** ST-24 (numbers), ST-7 (an infinite cost shows as ∞).
- **Scope:** new `modules/stats/core/include/tom/stats/core/stat_box_format.h`, `modules/stats/core/src/stat_box_format.cpp` and `modules/stats/tests/unit/stat_box_format_test.cpp`, with their lines in `modules/stats/CMakeLists.txt`.
- **Interfaces:**

  ```cpp
  enum class Rounding { Down, Up };

  // ST-24: below 10 000 the whole number, from 10 000 on three significant
  // digits with K, M, B or T; +inf as "∞".
  [[nodiscard]] std::string formatStatNumber(double value, Rounding rounding);
  ```

  - The value is first rounded to a whole number in the given direction (`Down` towards −∞, `Up` towards +∞). Below 10 000 in absolute value, that number is the text.
  - From 10 000 on: three significant digits, rounded in the same direction, with the largest of K (10³), M (10⁶), B (10⁹) and T (10¹²) that leaves at least one digit before the point. A rounding that carries to the next power of 1000 takes the next suffix: `10000` gives `10.0K`; `1638400` gives `1.64M` rounded up and `1.63M` rounded down; `999999` gives `999K` rounded down and `1.00M` rounded up.
  - From 1000T on, the whole number of T rounded in that direction (`2500T`, assumption 17).
  - A negative value gets `-` before the text of its absolute value, with the rounding applied to the signed value (−2.5 rounded down gives `-3`).
  - `+inf` gives `∞` (U+221E, UTF-8); NaN and `-inf` give `?` (assumption 17).
  - The function does not hide floating-point noise: C9b takes the tolerance off a cost before rounding it up.
- **Verification:** `unit` tests check that:
  - 0, 12, 9999, 9999.7 rounded down (`9999`) and rounded up (`10.0K`), and 100.00000000000001 rounded down (`100`) and up (`101`) come out as specified;
  - the examples above, 12 345 rounded down (`12.3K`), 1.5e9 (`1.50B`), 1e15 (`1000T`) and 2.5e15 rounded down (`2500T`) come out;
  - `+inf` gives `∞`, NaN and `-inf` give `?`, and −12 345 rounded down gives `-12.4K`;
  - for each pair (99.99, 100), (9999.5, 10000), (1639999, 1640000) and (999999, 1e6), the first rounded down shows a smaller number than the second rounded up, so the box never shows an XP amount equal to a cost it has not reached;
  - `scripts/test.sh`, the format check and lint pass.
- **Depends on:** T1.1. **Parallel** with every other task: it only adds files and their CMake lines.
- **Status:** merged (#35) with +inf as `∞`. The box's font has no ∞ ([S2 result](#s2-result)); C9d changes it to `--`.

#### C9b Stat box sections, labels and group order (C9 amendment)

- **Goal:** bring the merged C9 model in line with PR #31 and fix the two review findings of #29: a cost or an XP amount that rounding shows wrongly, and a box that could jump back to "Level Up" after the last point.
- **Covers:** ST-21 (labels on the buttons, namespace groups, group order, first page after the last point), ST-24 (sections, labels, number format, sections filled by a module), ST-26 (the provider registry, core part).
- **Scope:**
  - `modules/stats/core/include/tom/stats/core/stat_box_model.h`, `modules/stats/core/src/stat_box_model.cpp`, `modules/stats/tests/unit/stat_box_model_test.cpp`;
  - new `modules/stats/core/include/tom/stats/core/stat_box_sections.h`, `modules/stats/core/src/stat_box_sections.cpp`, `modules/stats/tests/unit/stat_box_sections_test.cpp`, with their CMake lines;
  - `modules/stats/core/include/tom/stats/core/stat_value.h`, `modules/stats/core/src/stat_value.cpp` (`kCostTolerance` only).
- **Interfaces:**
  - `StatBoxSections` (new) holds the section providers per namespace:

    ```cpp
    // The lines a module shows in the stat box section of its namespace's
    // group, in place of the stat lines (ST-24, ST-26).
    using SectionProvider = std::function<std::vector<std::string>()>;

    class StatBoxSections
    {
    public:
        // Sets the provider of the namespace, replacing an earlier one. Returns
        // false, and changes nothing, for an empty namespace, one with a colon
        // or an empty function.
        [[nodiscard]] bool set(std::string namespaceName, SectionProvider provider);
        void remove(std::string_view namespaceName);
        [[nodiscard]] const SectionProvider* find(std::string_view namespaceName) const;
    };
    ```

  - Where the registry lives: in its own class, which the plugin holds next to the service, and which A1 and A2 fill. Not in `StatsService`: the box is its only reader, and nothing in it is saved or reset by C8's `resetSaveState`, while C7a and C8 both change `stats_service.h` and `.cpp`, so a service change would wait behind both. Not in `StatBoxModel`: a model lives only while the box is open. Who may set which namespace is A1's to check.
  - Constructor: `StatBoxModel(StatsService& service, const Tables& tables, const StatBoxSections& sections, StatBoxLayout layout)`, in place of the `StatsTable` one, with `struct StatBoxLayout { std::size_t maxButtons; };`, which C9c extends. It reads each stat's group, display name and short name from `tables.stats` and the group order from `tables.statGroups` (C3a). The service, the tables and the sections must outlive the model.
  - In the constructor, the model calls the provider of each section the overview shows, once, on a copy of the `SectionProvider`, so a provider that removes itself does no harm, and keeps the lines for its lifetime (assumption 17). A provider that throws is caught: its section shows the stat lines, and `[[nodiscard]] const std::vector<Error>& errors() const` lists the failure for the log.
  - Label: `<display name> (<short name>)`, or the short name alone without a display name. Without a short name, the stat's name without its namespace is the short name. A stat without a stats-table entry has neither, and is in its namespace's group, or in Other without a namespace; only tests build one, since `registerStat` needs an entry (assumption 14).
  - First page: one section per group with an active stat, in the group order below, laid out as assumption 17 says. A stat's line is `<label> <value> (<xp> / <cost> XP)`, with `formatStatNumber` (C9a): the effective value and the XP rounded down, the cost rounded up after `cost · (1 − kCostTolerance)`. A section with a provider shows the provider's lines in place of the stat lines.
  - Group order, for the sections and for the group buttons of "Level Up": the groups of `tables.statGroups` except Other, then every other group of an active stat in the order of its first stat in `StatRegistry::activeStats()` (so the namespace groups come in registration order), then Other. A group without a stat on the page is left out.
  - Stat buttons on the "Level Up" pages show `<label> <value>`, e.g. `Intelligence (INT) 12`.
  - A press that assigns the last pending point sets the screen back to the first page (`screen_ = Screen::Stats`), so the box does not return to "Level Up" when points become pending again while it is open.
  - `kCostTolerance` moves from the anonymous namespace of `stat_value.cpp` to `stat_value.h` as `inline constexpr double kCostTolerance = 1e-9;`, with its comment, so `addXp` and the box use one value. The model's own `formatNumber` goes; C9a replaces it.
- **Verification:** `unit` tests check that:
  - with tables like the shipped default (display names, short names, groups and `statGroups` as C3a ships them), a player at the default base of 10 sees exactly:

    ```txt
    Warrior
    Strength (STR) 10 (0 / 100 XP)
    Constitution (CON) 10 (0 / 100 XP)
    Endurance (END) 10 (0 / 100 XP)

    Mage
    Intelligence (INT) 10 (0 / 100 XP)
    Wisdom (WIS) 10 (0 / 100 XP)

    Thief
    Dexterity (DEX) 10 (0 / 100 XP)
    Perception (PER) 10 (0 / 100 XP)

    Other
    Charisma (CHA) 10 (0 / 100 XP)
    ```

  - a registered `Luck:LUC` without display name and short name shows as `LUC 7 (0 / 100 XP)` in a `Luck` section, one with only a display name `Luck` as `Luck (LUC) ...`, and a stat without a stats-table entry by its name without the namespace;
  - registered stats `B:Y` and then `A:X`, both without a group, give sections in the order Warrior, Mage, Thief, B, A, Other; a registered stat with group Mage is in the Mage section; Other is last even when `statGroups` names it first;
  - a provider for `Luck` fills that section with exactly its lines, under the group name, and leaves the other sections alone; it is called once per model, also across presses; a provider that throws leaves the stat lines and one entry in `errors()`; a provider whose namespace has no active stat is not called and adds no section; after `remove` the stat lines are back;
  - the "Level Up" buttons read `Strength (STR) 10` and so on, and the group buttons follow the section order;
  - 99.99 XP toward a cost of 100 shows `99 / 100`; at r = 1.1 and one active point the cost `cost(1.1, 1)` (110.00000000000001) shows as `110`; a cost that overflows to infinity shows `∞`; 12 345 XP shows `12.3K`;
  - after the last point the box shows the first page; when `onPlayerLevelUp` then grants points while the same model is open, `page()` is the first page with "Level Up" and "Cancel", not the "Level Up" page;
  - `StatBoxSections::set` rejects an empty namespace, a namespace with a colon and an empty function; a second `set` replaces the first; `remove` of an unknown namespace does nothing;
  - the page transitions of C9 still hold with the new texts;
  - `scripts/test.sh`, the format check and lint pass.
- **Depends on:** C3a, C9a, and C8 merged (C8 changes `stat_value.h`). **Parallel** with C7a, which changes only the service files and their tests.
- **Status:** merged (#38); C9d changes its infinite-cost check from `∞` to `--`.

#### C9c Stat box pages (C9 amendment)

- **Goal:** split the overview and the level-up button lists into pages when they do not fit one box, as PR #33 has it. A task of its own, after C9b, to keep each review small; both change the same files.
- **Covers:** ST-21 (paged button lists), ST-24 (paged overview, "Back" and "Next", "OK" or "Level Up" and "Cancel" on every page).
- **Scope:** `modules/stats/core/include/tom/stats/core/stat_box_model.h`, `modules/stats/core/src/stat_box_model.cpp`, `modules/stats/tests/unit/stat_box_model_test.cpp`.
- **Interfaces:**
  - `StatBoxLayout` gains `std::size_t linesPerPage`, from `Settings::statBoxLinesPerPage` (C3b), so it is `{ maxButtons, linesPerPage }`. The model raises a `maxButtons` below 4 (the most one overview page needs: "Back", "Level Up", "Cancel", "Next") to 4 and a `linesPerPage` below 2 to 2.
  - Overview pages, by assumption 17: the sections laid out by C9b, filled into pages of at most `linesPerPage` lines; a section longer than a page spread evenly over pages of its own, each headed `<group> (<i> / <k>)`. Buttons: "Back" from the second page on, then "OK", or "Level Up" and "Cancel", then "Next" on every page that has a following one.
  - Level-up pages: a button list that does not fit (`n + 1 > maxButtons` for `n` groups, or for one group's `n` stats) is split by assumption 17, with "Back" first and "Next" last. The page text is `Level Up (<i> / <k>)` for the group list or `<group> (<i> / <k>)` for a group's stats, then `Free points: <n>`; an unpaged list keeps C9b's text. The unpaged lists get "Back" first as well (user decision, assumption 17).
  - The model keeps the page index of the overview, the group list and the group; a page is rebuilt on each press, and an index past the last page is clamped to it. After the last point the box shows the first overview page (ST-21).
- **Verification:** `unit` tests check, with exact page texts and button rows, that:
  - with the shipped-default tables (C9b's 15 lines) and `linesPerPage` 15, there is one page with buttons `OK`, or `Level Up`, `Cancel`;
  - with `linesPerPage` 8, page 1 is Warrior, an empty line and Mage (8 lines) with buttons `OK`, `Next`, and page 2 is Thief, an empty line and Other with buttons `Back`, `OK`; with points pending they are `Level Up`, `Cancel`, `Next` and `Back`, `Level Up`, `Cancel`; "Next" and "Back" move between them;
  - a section exactly at the limit (`linesPerPage` 4 and Warrior's 4 lines) fills one page unsplit, without a part in its heading; with `linesPerPage` 4 the default gives four pages, the middle ones with `Back`, `OK`, `Next`;
  - a `Luck` provider with 7 lines and `linesPerPage` 4 gives `Luck (1 / 3)` with 3 lines, then `Luck (2 / 3)` and `Luck (3 / 3)` with 2 each, each on a page of its own, and the next section starts a new page; 7 lines at `linesPerPage` 5 give pages of 4 and 3 lines;
  - with room for all buttons, the "Level Up" page reads `Back`, `Strength (STR) 10`, ..., `Charisma (CHA) 10`, and an unpaged group page `Back` and then its stats; "Back" still leaves the page as in C9;
  - with `maxButtons` 4 and the 8 core stats pending a point, "Level Up" shows `Level Up (1 / 2)` with `Back`, `Warrior`, `Mage`, `Next`, and `Level Up (2 / 2)` with `Back`, `Thief`, `Other`; "Back" on the first page returns to the overview page it came from, on the second to the first;
  - three registered stats in Warrior, with `maxButtons` 4, give `Warrior (1 / 3)`, `(2 / 3)` and `(3 / 3)` with two stat buttons each between `Back` and `Next` (no `Next` on the last), and a point assigned on page 2 stays on page 2;
  - assigning the last point from a later page of a group shows the first overview page; a `maxButtons` of 3 and a `linesPerPage` of 1 behave as 4 and 2;
  - `scripts/test.sh`, the format check and lint pass.
- **Depends on:** C9b, C3b. **Parallel** with C7a.

#### C9d Infinite cost as `--` (C9a amendment)

- **Goal:** show an infinite cost as `--` instead of `∞`, since the message box font has no ∞ ([S2 result](#s2-result), PR #41).
- **Covers:** ST-24 (an infinite cost), ST-7.
- **Scope:** `modules/stats/core/include/tom/stats/core/stat_box_format.h`, `modules/stats/core/src/stat_box_format.cpp`, `modules/stats/tests/unit/stat_box_format_test.cpp`, `modules/stats/tests/unit/stat_box_model_test.cpp`.
- **Interfaces:** `formatStatNumber` keeps its signature; `+inf` gives `--` in both rounding directions, and the header comment says so. NaN and `-inf` still give `?`. `kInfinity` and its UTF-8 bytes go.
- **Verification:** `unit` tests check that `+inf` gives `--` rounded down and up, NaN and `-inf` still give `?`, and C9b's infinite-cost line reads `Strength (STR) 2010 (0 / -- XP)`; no `∞` (`\xE2\x88\x9E`) is left under `modules/stats` (`grep -rn $'\xE2\x88\x9E' modules/stats` finds nothing); `scripts/test.sh`, the format check and lint pass.
- **Depends on:** C9a, C9b (both merged) and PR #41 merged, so the PRD says `--`. **Parallel** with every other task: no open task changes these files.

#### C10 Incompatible-mod detection and warning (core part of ST-28)

- **Goal:** the game-independent part of ST-28 as pure functions, so K1 only adapts the game's plugin lists, the message box and the file write: which entries are detected, which of them the warning names, its text, the list after "Don't show again", and the INI text with that list written in (assumption 18).
- **Covers:** ST-28 (detection, warning content, dismissal per mod), ST-27 (writing the one setting back).
- **Scope:** new `modules/stats/core/include/tom/stats/core/compatibility.h`, `modules/stats/core/src/compatibility.cpp`, `modules/stats/core/include/tom/stats/core/config/ini_edit.h`, `modules/stats/core/src/config/ini_edit.cpp`, `modules/stats/tests/unit/compatibility_test.cpp`, `modules/stats/tests/unit/ini_edit_test.cpp`, with their lines in `modules/stats/CMakeLists.txt`.
- **Interfaces** (names may follow the existing style):

  ```cpp
  // compatibility.h
  // The names of the entries with a loaded game plugin or SKSE plugin, in
  // table order. File names compare case-insensitively (ASCII); a game
  // plugin is its name plus extension, e.g. "Requiem" ".esp".
  [[nodiscard]] std::vector<std::string> detectIncompatible(const IncompatibleTable& table,
      std::span<const PluginInfo> gamePlugins, std::span<const std::string> sksePlugins);

  // The detected mods the warning names: those not in `dismissed`, in the
  // order of `detected`; none when the override of ST-28 is set.
  [[nodiscard]] std::vector<std::string> warnedMods(std::span<const std::string> detected,
      std::span<const std::string> dismissed, bool allowIncompatibleMods);

  // The warning's body text, naming each mod on a line of its own.
  [[nodiscard]] std::string warningText(std::span<const std::string> mods);

  // The buttons, in order: "OK", "Don't show again".
  enum class WarningButton { Ok, DontShowAgain };

  // `dismissed` with each of `named` appended that it does not hold yet.
  [[nodiscard]] std::vector<std::string> withDismissed(std::span<const std::string> dismissed,
      std::span<const std::string> named);

  // config/ini_edit.h
  // Returns `text` with `key` in `section` set to `value`. Sections and keys
  // match case-insensitively, as loadSettings reads them. Only the value of
  // the first matching key line changes; a missing key is added after the
  // last key line of the first matching section, a missing section at the
  // end. All other bytes stay as they are; an added line uses the line ending
  // the text already uses (CRLF or LF).
  [[nodiscard]] std::string setIniValue(std::string_view text, std::string_view section,
      std::string_view key, std::string_view value);
  ```

  - `warningText` says that the module is disabled because of the mods it names (ST-28, OQ-8 closed); the exact text is fixed in the test. With the default overview's limits in mind ([S2 result](#s2-result)), the text needs one line per mod plus at most three lines, each under about 40 characters, so a warning for every default entry still shows at full size.
  - K1 writes the list with `formatNameList` (C3c) and `setIniValue(text, "Compatibility", "DismissedWarnings", ...)`.
- **Verification:** `unit` tests check that:
  - `detectIncompatible` finds an entry by its game plugin (`requiem.ESP` matches `Requiem.esp`), by its SKSE plugin, and by both only once; an entry none of whose files is loaded, and a race overhaul's plugin that is not in the table (`Imperious - Races of Skyrim.esp`), give nothing; the result follows the table order;
  - `warnedMods` of `Requiem`, `Valhalla Combat` with `Valhalla Combat` dismissed is `Requiem`; with both dismissed it is empty; a dismissed name that is not detected changes nothing; the match is case-sensitive; with `allowIncompatibleMods` true it is empty whatever is dismissed;
  - `warningText` of two mods is the exact expected text, with each name on a line of its own;
  - `withDismissed` of `A` and `B, A` is `A, B`; of an empty list and `A` is `A`;
  - `setIniValue` replaces only the value in `DismissedWarnings = Requiem` and leaves the comments, the other keys and sections and the line endings byte for byte; it matches `[compatibility]` and `dismissedwarnings`; it adds the key to a `[Compatibility]` section without it, after `AllowIncompatibleMods`; it appends the section to a text without it, and to an empty text; it keeps CRLF in a CRLF file; a key of the same name in another section is left alone;
  - for each of these, `loadSettings` (C3) of the result reads the new list and every other setting unchanged, with no errors;
  - `scripts/test.sh`, the format check, lint and the sanitizer run pass ([CONTRIBUTING.md#workflow](../../CONTRIBUTING.md#workflow)).
- **Depends on:** C3c (`parseNameList`, `formatNameList`, and the name rule). **Parallel** with every other task: it only adds files and their CMake lines.


### Phase 3: Spikes (uncertain game hooks)

Each spike is a throwaway prototype in the plugin with a time box. Its result is a short update of the affected task in this plan (hook point, Address Library ID, fallback), which the user approves. The spikes depend on T1.2 and can run in parallel with each other and with Phase 2.

| ID | Question | Feeds |
| --- | --- | --- |
| S1 | Can the vanilla LevelUp menu be suppressed while the level and the perk point are still applied right away and the +10 is never granted (ST-19)? Where is the moment the menu would open? Does it behave the same with Experience and with a level-up menu mod? Fallback if suppression fails: let the vanilla flow run and revert the +10 (must be invisible to the player). | G5 |
| S2 | Can a native message box open over the skills menu with SkyUI and without it, and can a key or gamepad button be caught only while that menu is open (ST-24)? Are the default key K and gamepad button Y free while the skills menu is open, with and without SkyUI, so that pressing them there does nothing else? If not, report it before U1 starts; changing the defaults is the user's decision. How many buttons fit in one box (ST-21 grouping)? How many lines of text does the box show before it cuts off or scrolls, with and without SkyUI, and does a long line wrap and take two (the default of `LinesPerPage`, C3b)? How many buttons fit with SkyUI? How long can a button text be (`Constitution (CON) 10`), and does the box's font have ∞ (U+221E)? The sectioned overview of C9b has 15 lines with the core stats alone. Is a hint possible without changing the skills menu (ST-25)? | U1, C9d (U2, dropped for v1, D8) |
| S3 | Where can skill XP be multiplied so that it stacks multiplicatively with Guardian Stones, Fortify effects and other mods (ST-12, ST-17), instead of replacing them or being overwritten? | G6 |
| S4 | Where can incoming damage be split before it reaches Health, separately for physical and magic damage (ST-11)? Does that hook coexist with combat overhauls? | G7 |
| S5 | How can DEX set the base crit chance for physical attacks and for spells, given that vanilla has no spell crits (ST-13)? | G8 |
| S6a | Hook points for every default training action of ST-15. | G9a to G9c |
| S6b | Hook points for the default checks of ST-15. Persuasion and intimidation are dialogue conditions in vanilla. | G10a to G10c |
| S7 | How does the module learn about Custom Skills Framework skill level-ups (ST-16)? Do skill level-up events still fire above 100 with Beyond Mastery and Experience? Where does CSF compute the XP of a skill use (its `Skill::Advance` and its own Mod Skill Use perk entry call), and where can it be multiplied after that call, for uses only, as the [S3 result](#s3-result) does for vanilla skills (ST-12)? | G4, G6 |
| S8 | Papyrus build with the Creation Kit compiler under Wine (D5). This starts from the current error "Wine Mono is not installed": installing Wine Mono or .NET in the Wine prefix is a **user action** (done, [S8 result](#s8-result)). Then compile a stub script with a native function from a CMake custom command, with the Skyrim path as a cache variable, and bind it from the plugin. | A2 |
| S9 | Sneak reveal (ST-13a, PR #55): how can a sneaking NPC within a range of the player get a visual cue without an ESP? Candidates: an effect shader on the NPC (a vanilla shader form, or one made at runtime), a HUD or compass marker. How does the plugin find sneaking NPCs near the player (the process lists, `Actor::IsSneaking`), how often, on which thread and at what cost; how is a line-of-sight test done, should OQ-12 ask for one; and how is the cue removed when the NPC stops sneaking, leaves the range or the cell unloads? The result lists the options with screenshots for the user, who picks the cue (OQ-12). | G11 |
| S10 | END's small effects (ST-11, PR #55): fall damage mitigation, breath duration and resistance to poison. Fall damage reaches `DoDamage` from 37998+0xC7 with no attacker ([S4 result](#s4-result)): where is its amount computed, is 37998 the only path (player and NPCs), and can the damage be scaled at that call, before the difficulty multiplier? Does skyrim-acrobatics-ng, which is in the test install, change fall damage, and where? Breath: where does the game count the time under water before drowning (37348 deals the damage), which values set it, and can it be lengthened or shortened per actor? Resistance to poison is an actor-value effect on `PoisonResist` (ST-10): confirm that a permanent modifier on it lowers a poison's magnitude through `CheckResistance` ([S6a result](#s6a-result)) and how the game caps it, so that no hook is needed. | G7a, G7b, C5b |

#### S1 result

Run on 2026-11-24 on 1.7.104 with the test install's Experience, through the skyrim-mcp bridge in `qasmoke`. **Suppression works; the revert fallback is not needed.** The prototype is `modules/stats/plugin/src/LevelUpHook.cpp`.

How vanilla levels up (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE):

- Reaching the XP threshold only shows "Level up" on the HUD. The level-up itself happens in `StatsMenu::ProcessMessage` (52510) on a menu update, while the skills menu is open and `PlayerSkills::CanLevelUp` holds (XP at or above the threshold). In order, it:
  1. shows the "Leveling Up" tutorial once per save (52692 with tutorial `0xFA`, call at +0xFFA);
  2. raises the level and sends `LevelIncrease::Event` (41566);
  3. queues the LevelUp menu (51916, call at +0x1012);
  4. adds one perk point (52538).
- Confirming an attribute (`ConfirmLevelUpAttributeCallback::Run`, 51917) adds the +10 (`iAVDhmsLevelUp`; Stamina also raises carry weight by `fLevelUpCarryWeightMod`), then calls `PlayerSkills::AdvanceLevel(false)` (41567). That takes the threshold off the XP and computes the next threshold. If the XP still covers it, the next menu update repeats the flow.
- So the moment the LevelUp menu would open is when the skills menu opens with a level-up pending, and at that moment the level and the perk point are already applied by vanilla code.

The hook:

- Replaces the call at 52510+0x1012 with a function that calls `AdvanceLevel(false)` through the confirm callback's own call, not 41567 directly, because Experience redirects that call. The call target is read again on each level-up.
- Replaces the tutorial call at 52510+0xFFA with a no-op, since that tutorial asks the player to choose Health, Magicka or Stamina.
- Finds each site by scanning for exactly one call to the known target. The confirm callback's call is the one after `mov rcx, [rax + offset of PlayerCharacter::skills]`, with the offset taken from CommonLibSSE-NG's layout at runtime (0x9C0 on 1.7.99 and later). If a site is not found, for example because another plugin patched it, nothing is patched and the log says why.

Evidence (plugin log, console `player.getlevel` and `player.getbaseav`, screenshots). The first three runs are from the build before the review fixes (runtime skills offset, target re-read); the last is a re-test of the fixed build from the main session:

| Run | Result |
| --- | --- |
| Hook not installed (baseline), Experience `player.advlevel 500` | Skills menu: `LevelIncrease` to 2, then the LevelUp menu opens with level 2, perk points 1, XP 501/100. Choosing Stamina: Stamina 105 to 115, XP 401/125, and the next LevelUp menu opens at once. The "Leveling Up" tutorial shows first. |
| Hook, Experience `player.advlevel 500` | Three level-ups in one menu opening, each finished by the hook. Level 1 to 4, "Perks to increase: 3", XP 500/100 to 125/175. Health 110, Magicka 85, Stamina 105, carry weight 325 unchanged. No LevelUp menu. The tutorial still showed (fixed in the next run). |
| Hook with the tutorial patch, Experience skill XP (`bEnableSkillXP = true`, `player.advskill` on six skills) | Level 1 to 9 within about 0.3 s, "Perks to increase: 8", Health, Magicka, Stamina and carry weight unchanged, no LevelUp menu and no tutorial. No crash log. |
| Fixed hook, Experience, re-test on 2026-11-24 on 1.7.104 | Level 1 to 4, 3 perk points, Health, Magicka, Stamina and carry weight unchanged, no LevelUp menu, no tutorial, no crash. |

Not covered:

- **The game without Experience:** the test install always loads it; a run with `Experience.dll` shadowed by an empty file never got past startup. Experience's skill-XP mode stands in for the vanilla XP source.
- **Level-up menu mods:** none is installed. A mod that only replaces `levelupmenu.swf` is never shown. A plugin that patches the same calls makes the scan fail, so the hook stays out (K1 or a compatibility note).
- **1.6.x:** untested. Nothing is tied to a 1.7.x layout or offset: the IDs are AE IDs, the call offsets come from the scan and the skills offset from CommonLibSSE-NG (0x9B8 before 1.7.99). Whether the 1.6.x functions keep the same instruction shapes is not checked.
- **Level changes outside this flow:** `player.setlevel` (41563) and the vanilla `AdvancePCLevel` set the level directly, without `LevelIncrease`, a perk point or a menu. `UpdateLevel` (22369) runs 41566 and `AdvanceLevel` in a loop, so it sends `LevelIncrease` but grants no perk point. Per [ST-18](PRD.md#75-level-up) these bypass the level-up: they grant no free points and don't advance stats. Only the hook path counts, so G5 must not count `LevelIncrease` events.

#### S2 result

Run on 2026-11-26 on 1.7.104 with SkyUI (`SkyUI_SE.esp` in the test install), at 1280×720, through the skyrim-mcp bridge in `qasmoke`. **A native message box opens over the skills menu, and K is caught only while that menu is open; the box needs vertical buttons, and the font has no ∞.** The prototype is `modules/stats/plugin/src/StatBoxSpike.cpp` and `.h` on the branch `stats/s2-stat-box-spike` (06bdd17). That branch stays unmerged, as the reference for U1.

How the prototype works (Address Library IDs are AE):

- An input sink on `RE::BSInputDeviceManager` and a `RE::MenuOpenCloseEvent` sink. K (DIK 0x25) and the gamepad's Y (0x8000) are handled only while `RE::StatsMenu` is open.
- The box is a `RE::MessageBoxData` from `RE::MessageDataFactoryManager::GetCreator<RE::MessageBoxData>(RE::InterfaceStrings::messageBoxData)->Create()`, with `bodyText`, `buttonText` and a callback (an `RE::IMessageBoxCallback` made with `make_smart`) filled in, then queued with `RE::MessageBoxMenu::QueueMessage` (52271). The factory's defaults: `warningType` 0, `cancelButtonIndex` −1, `menuDepth` 10, `buttonPressOffset` 0, `useHtml` false, `verticalButtons` false, `isCancellable` false.
- The hint test showed a HUD message with `RE::SendHUDMessage::ShowHUDMessage` when the skills menu opened.

Evidence (plugin log, screenshots of the session, which are not kept; the skills menu opened with Tab, Up, Return):

| Check | Result |
| --- | --- |
| K in gameplay | Logged as seen with the skills menu closed and ignored; nothing visible happens. |
| K in the skills menu | Its mapped user event there is empty (no action), so K is free. |
| Box over the skills menu | Opens over it; the skills menu stays open behind the box and after it is answered. The callback gets the index of the chosen button (Down three times and Return give button 3). |
| Lines of text | The box never cuts off or scrolls; it shrinks the font to fit. 16 lines (15 and one more) show at full size; about 31 lines (24, a long line that wraps, and one more) are smaller but readable; 41 lines are too small to read. |
| Long lines | A line wraps at about 40 characters; about 200 characters took 5 lines. |
| ∞ (U+221E, UTF-8) | Shows as an empty box: the font has no glyph for it. *User decision:* an infinite cost shows as `--` (ST-24, PR #41). |
| Buttons side by side (default) | 3 fit easily; 6 labels like `Constitution (CON) 1` fit across; 8 overflow both screen edges, with the box stretched to the full width. |
| `verticalButtons = true` | 10 buttons and 16 lines of text fit, with the font slightly smaller. *User decision:* the box stacks its buttons (ST-24, PR #41). |
| Hint (ST-25) by HUD message | A HUD message sent when the skills menu opens is not visible over the skills menu, nor after it closes. |

Not covered (*user decision:* the user checks the first three by hand later; U1 lists them as manual checks):

- **Gamepad Y:** the bridge cannot send gamepad input, so whether Y is caught and free in the skills menu is not checked.
- **The game without SkyUI:** the bridge cannot turn plugins off. Whether the box opens over the vanilla skills menu, and whether its line and button limits are the same, is not checked.
- **Other resolutions:** only 1280×720 was tried.
- **Other hint routes (ST-25):** only the HUD message was tried.

#### S3 result

Run on 2026-11-27 on 1.7.104 with Experience, Custom Skills Framework (CSF), Beyond Mastery and SkyUI, through the skyrim-mcp bridge in `qasmoke`. **Multiplying the XP right after the game's perk entry for skill use stacks multiplicatively with Guardian Stones, Rested and Lover's Comfort, and with Experience and CSF loaded.** The prototype is `modules/stats/plugin/src/SkillXpSpike.cpp` and `.h` on the branch `stats/s3-skill-xp-spike` (3ea4935), with the trampoline raised from 28 to 56 bytes in `main.cpp`. That branch stays unmerged, as the reference for G6.

How vanilla computes skill XP (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE):

- Every skill use goes through the virtual `Actor::UseSkill` (vtable slot 0xF7), which for the player is `PlayerCharacter::UseSkill` (40488, CommonLibSSE-NG's `AddSkillExperience`), reached through the `PlayerCharacter` vtable (208040). `player.advskill` (22367) calls the same slot with no object and action 0, so it takes the full use path.
- 40488 calls the skill XP routine (41561) at +0x25 with "is a use" set. In 41561, in order:
  1. the cap check (Beyond Mastery patches it at +0x6F);
  2. the skill's AVSK record through 27244;
  3. for a use, XP = `useMult × points + useOffset` (+0xF1);
  4. writes `advanceSkill` (player+0xAF8 on 1.7.104, read from the code);
  5. `BGSEntryPoint::HandleEntryPoint` (23526) with entry point 0x16, Mod Skill Use, on the address of the XP (call at +0x158);
  6. adds the XP and runs the level loop.
- A whole level granted at once (skill books, trainers, `incpcs`) goes through `PlayerSkills::IncrementSkill` (41562), which calls 41561 at +0x98 with "is a use" cleared. XP is then what is missing to the next level, and step 5 still runs.
- The vanilla skill XP bonuses are Mod Skill Use (0x16) Multiply perks, so all of them act inside that one `HandleEntryPoint` call, and several of them multiply:
  - Guardian Stones, ×1.2 for their skills: Warrior (spell 000E5F4C, perk 000E5F4A), Mage (000E5F47, 000E5F49), Thief (000E5F45, 000E5F46); Lover (000E5F5A, 000E5F57), ×1.15 for all skills;
  - Rested (000FB981) ×1.05, Well Rested (000FB984) ×1.10, Lover's Comfort (000CDA1D) ×1.15.
- Fortify <Skill> effects raise the skill level, not the XP. The `<Skill>SkillAdvance` actor values are not used by vanilla bonuses.

The prototype's hooks:

1. `use_skill`: `write_vfunc` on vtable 208040, slot 0xF7, calling the previous value.
2. `xp_routine_call`: the call at 40488+0x25, found by scanning for the unique bytes `C6 44 24 28 01 89 44 24 20` followed by an `E8` within 0x40 bytes. Any call target is accepted and called, since Experience redirects it. It sets a thread-local "inside a use" flag around the call.
3. `perk_entry`: the call at 41561+0x158, found by scanning for the unique `B9 16 00 00 00` followed by an `E8` to 23526 within 0x400 bytes. It applies its factor after the perk entry only while the flag of hook 2 is set, so whole-level grants are left alone.

Evidence (plugin log, console). Base XP is One-Handed's `useMult` 6.3 with `useOffset` 0:

| Check | Result |
| --- | --- |
| Install log | Experience redirects both 40488+0x25 and 41562+0x98 (to `Experience.dll`+0x56aa). Vtable slot 0xF7 already held `CustomSkills.dll`+0x319a0 (CSF hooks `UseSkill`); hook 1 chained behind it. The `useMult × points` bytes at +0xF1 are vanilla. Hook 3 is at 41561+0x158. No site was missed. |
| `player.advskill onehanded 10` | 63 XP, perk factor ×1.0. |
| + Warrior Stone (`player.addspell 000E5F4C`) | 75.6 (×1.20). |
| + Well Rested (000FB984) | 83.16 (×1.32 = 1.2 × 1.1: the perk entries multiply). |
| Alteration with both | 33 (×1.10: the Warrior Stone does not cover it). |
| Factor ×2 at hook 3 (`perk_entry`) | 166.32 = 63 × 1.32 × 2. |
| Factor ×2 at hook 1 (`use_skill`) | points 10 to 20, 126 before the perks, 166.32 after. |
| Factor ×2 at hook 2 (`xp_routine_call`) | 166.32. |
| `player.incpcs onehanded` (whole level) | 343.455 to 453.361 (×1.32 from the vanilla perks); the ×2 of hook 3 not applied. Logged with no `use_skill` or `xp_routine_call` line. |

All three hooks stack multiplicatively with the perks. Hooks 1 and 2 scale the points before `useOffset` is added, so they are purely multiplicative only for a skill whose `useOffset` is 0, as One-Handed's is. Hook 3 scales the whole XP after the perks, whatever the offset, which is why G6 uses it.

Not covered:

- **A real skill use:** the bridge's mouse clicks did not cast a spell (Magicka stayed at 85), so only `player.advskill` was used. It takes the same vtable path, but G6 checks a real use by hand.
- **Fortify potions:** not tried in game; per the disassembly they raise the level and do not touch the XP.
- **A skill at Experience's cap**, and Beyond Mastery's cap check above 100.
- **CSF custom skills:** their XP goes through CSF's own `Skill::Advance`, not 40488 or 41561, so hook 3 does not see it. This belongs to S7 and G6 (assumption 9).
- **The thread** the hooks run on was not recorded (assumption 15).
- **1.6.x:** untested. The IDs are AE IDs and the sites come from the scans; whether the 1.6.x functions keep the same byte patterns is not checked.

#### S4 result

Run on 2026-11-27 on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng, through the skyrim-mcp bridge in `qasmoke`, at difficulty 2 (Adept, whose multiplier left the numbers as they were); no crash log. **Health damage can be split at one call inside `Actor::DoDamage`, after the difficulty multiplier, for physical and magic damage alike; nothing in the test install patches the damage path.** The prototype is `modules/stats/plugin/src/DamageSplitSpike.cpp` and `.h` on the branch `stats/s4-damage-split-spike` (9268ab5), with the trampoline raised from 28 to 104 bytes in `main.cpp`. That branch stays unmerged, as the reference for G7.

How Health damage reaches an actor (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE):

- A melee hit (38627) fills a `HitData` with `HitData::Populate` (44001): the attack damage and the attacker's perk entries, then 44016 (Mod Attack Damage, the target's Mod Incoming Damage, crits, spike S5), 44014 (armor, blocking) and 44015. It then calls `Actor::ProcessHit` (38586) at 38627+0x4A8, the call combat overhauls usually redirect. Projectile hits reach 38586 from other callers.
- 38586 calls `Actor::DoDamage` (37335) at +0x4A7 with `HitData::totalDamage`, so armor, blocking and perks are in it.
- Magic Health damage goes through `ValueModifierEffect::ModifyActorValue` (35086, vtable slot 0x20, shared by the accumulating and peak variants): for Health and a negative value it calls 37335 at +0x232 with the caster. Resistances and Mod Incoming Spell Magnitude are already in the effect's magnitude. Damage over time calls it on every update.
- 37335 (target, damage, source, `dontAdjustDifficulty`) applies the difficulty multiplier (26503), then calls the actor-value change with a cause (38468) at +0x151 with the damage modifier, Health and −damage. Fall damage (37998+0xC7) and drowning (37348) call 37335 too, with no attacker.
- 38468 changes the value, runs the death checks, and only then calls `Actor::HandleHealthDamage` (vtable slot 0x104, 38606), which sees the damage after Health has changed: too late for a split.
- The console's `damageav` and Papyrus `DamageActorValue` go through the actor-value owner's `ModActorValue` (38467), which calls 38468 directly, past 37335.

The prototype's hooks (a JSON file in `Data/SKSE/Plugins` sets the share, the hook that splits and the pool, and is read again when it changes):

1. `hit`: the call of 37335 at 38586+0x4A7, found by its unique register setup before the call; a trampoline stub passes the `HitData` (r13) on. It marks the damage as physical for hook 3.
2. `magic`: the call of 37335 at 35086+0x232, found the same way; a stub passes the effect (rdi) on. It marks the damage as magic for hook 3.
3. `health`: the call of 38468 at 37335+0x151, after the difficulty multiplier. It reads the kind hooks 1 and 2 set (thread-local, for the one `DoDamage` call); a call from anywhere else is "other". The split takes `min(share × damage, pool)` off the pool (Stamina for physical, Magicka for magic) with `DamageActorValue` and passes the rest on to Health. Only the player is split.

Before patching, the prototype checks that the entries of 44001, 38586, 37335, 35086 and 38468 are not detoured and that the vtable slots are vanilla.

Evidence (plugin log; the player's Health stayed near its maximum, set high for the run). A bandit with a Steel Mace hits for 7.50, 15.00 with a power attack:

| Check | Result |
| --- | --- |
| Install log | All three sites found, one vanilla call each; no entry detoured; `HandleHealthDamage` (slot 0x104 of `PlayerCharacter` 208040 and `Character` 207886), `ModActorValue` (slot 6 of 208050) and `ModifyActorValue` (slot 0x20 of 206143) vanilla; the melee call at 38627+0x4A8 goes to the vanilla 38586. |
| Share 0, bandit hits | Hook 1 sees 7.50 (after armor, block and perks), hook 3 the same 7.500 as physical. |
| Share 0.5 at `health`, full Stamina | 7.500 → 3.75 into Stamina, 3.75 to Health; a power attack 15.000 → 7.50 and 7.50. |
| Partial pool (power attack, 4.97 Stamina) | 4.97 of 7.50 taken, 10.03 to Health, Stamina 0. |
| Empty pool (`staminarate` 0) | 0 of 3.75 taken, 7.50 to Health. |
| Firebolt from the bandit (share 0.5) | Hook 2 then hook 3 as magic: 25.000 at once, then the burn in ticks, 54 calls over about 1 s and 27.41 in total; each tick split on its own, 13.69 redirected into Magicka (85.00 to 71.29). |
| Fall damage (`setpos z` +1000) | Hook 3 only, no attacker, kind other: 118.967, not split, Health 1000 to 881.03. |
| `damageav`, Papyrus `DamageActorValue` | No line: they bypass 37335. |
| Threads | Hits on 436 to 448, the Firebolt ticks on 380 (the thread of `kPostLoad`), the fall on 440; install on 584 (`kDataLoaded`). |

Not covered:

- **A lethal hit:** 38468 runs the death checks after the split value is applied, so a split hit that kills was not seen.
- **Damage reduced to 0:** with a share that leaves no Health damage, whether 38468 still calls `HandleHealthDamage` (hit reactions, G9b's damage taken) is not checked.
- **Projectile hits:** the callers of 38586 other than the melee hit are not mapped; hook 1 sits in 38586 itself, so they pass it, but no arrow hit the player.
- **Poison, drowning, Dual and Absorb effects:** none reached the player in the run.
- **Combat overhauls:** none is installed. Hook 3 is inside 37335, so an overhaul that redirects the call at 38627+0x4A8 and still calls 37335 passes it; one that replaces 37335 or the call at +0x151 makes the scan fail.
- **1.6.x:** untested; the sites come from scans, as in the S3 result.

#### S5 result

Run on 2026-11-28 on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng, through the skyrim-mcp bridge in `qasmoke`; no crash log. Bandits fought hagravens, and a bandit cast at the player, since the bridge's player cannot reach an NPC with an attack. **DEX can be added to the physical crit chance right after the game's crit perks, at one call in the crit step every hit goes through; spells can crit once per effect, by scaling its magnitude after the game's spell perks.** The prototype is `modules/stats/plugin/src/CritSpike.cpp` and `.h` on the branch `stats/s5-crit-spike` (0ea0807). That branch stays unmerged, as the reference for G8.

How vanilla rolls a physical crit (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE):

- Every `HitData` builder (44001 melee, 44002 projectiles, 44004) calls 44016 on the `HitData`. After Mod Attack Damage (0x23), Mod Incoming Damage (0x24) and the sneak attack multiplier, and unless the hit has `kIgnoreCritical`, 44016:
  1. calls 26441 (attacker's actor-value owner, weapon, condition) at +0x28C for the base chance in percent: the attacker's `CriticalChance` actor value, times `(condition + 1) × fWeaponConditionCriticalChanceMult` (0.1) for a weapon that is not hand to hand, divided by the fire rate of an automatic weapon, times the weapon's CRDT % Mult if that is not negative. 26441 has no other caller;
  2. runs the attacker's Calculate My Critical Hit Chance (0x01) at +0x2A6 and the target's Mod Enemy Critical Hit Chance (0x11) at +0x2CB on that chance. Bladesman, Deep Wounds and Critical Shot *set* it (10, 15, 20); the Blades' blessing adds 10;
  3. rolls at +0x2EF: crit if the game's random number (68276) modulo 1000 is below the chance × 10, truncated. It sets `kCritical`;
  4. on a crit, takes the crit damage (the weapon's CRDT damage, or `fCombatUnarmedCritDamageMult` of the damage unarmed), runs Calculate My Critical Hit Damage (0x02) at +0x40B, and adds the result to `totalDamage`; the crit effect is the weapon's CRDT spell;
  5. then applies the kill-move and `AttackDamageMult` multipliers. Armor and blocking (44014) come later.
- Spells have no crit. `ActiveEffect::AdjustForPerks` (vtable slot 0; 34053, shared by the value modifier effects, 34316 for the dual one) runs once when `MagicTarget`'s effect creation (34547) adds an effect with a caster: it applies Mod Spell Duration and Magnitude (0x1E, 0x1D) and Mod Incoming Spell Duration and Magnitude (0x2A, 0x29). `ModifyActorValue` (35086) then applies the magnitude, once for an instant effect and on every update for damage over time ([S4 result](#s4-result)).

The prototype's hooks (a JSON file in `Data/SKSE/Plugins` sets the mode, the chances, the spell crit multiplier and whether NPC attackers count, and is read again when it changes):

1. `base`, `perks`, `roll` and `crit damage`: the calls at 44016+0x28C (26441), +0x2CB (0x11 perk entry), +0x2EF (68276) and +0x40B (0x02 perk entry), each found by its unique register setup before the call; a stub passes the `HitData` (r15) to the first. `base` can replace or add to the base chance, `perks` can add after the perk entries (the mode G8 uses), `roll` logs the roll, `crit damage` the damage added.
2. `adjust`: slot 0 of the vtables of `ValueModifierEffect` (206143), `AccumulatingValueModifierEffect` (205803), `PeakValueModifierEffect` (206014) and `DualValueModifierEffect` (205876), calling the previous function. After it, a detrimental Health damage effect with a caster rolls once, and on a crit its magnitude is multiplied.
3. `tick`: the call of `DoDamage` at 35086+0x232, one roll per effect on its first tick, kept as a fallback for comparison.

Evidence (plugin log):

| Check | Result |
| --- | --- |
| Install log | All four 44016 sites, the four vtable slots and the tick site found, all vanilla. |
| Vanilla chance, bandits and hagravens | `CriticalChance` 0 for every NPC, so the base chance is 0 % and no hit crits; condition 1. All five weapons used (Iron Sword, Iron War Axe, Steel War Axe, Steel Mace, unarmed) logged CRDT % Mult 1.00. |
| Base chance replaced with 50 % (NPC attackers) | 90 crits in 186 rolls (48 %). A crit adds the weapon's crit damage, e.g. a Steel Mace: 5 → 5 after the 0x02 perk entry, `totalDamage` 16.50 → 21.50. |
| Several computations per hit | Some hits ran 44016 two or three times within one millisecond, with the same attacker, target and damage (#90 and #91; #280 to #282), each with its own roll. |
| Spells at `adjust`, 50 %, multiplier ×2 | A bandit's Firebolts on the player: three effects, one roll each (72.47, 46.05, 81.20); the crit took the magnitude from 25 to 50. Its Flames (a concentration spell): one effect, one roll (1.36), 8 → 16. |
| Other Health damage effects | A fire hazard (spell 000153BD "Fire", casting source 3, the bandit as caster) also rolled at `adjust`. |
| Threads | The 44016 hooks on worker threads 428 to 440; `adjust` on 428 to 440, and on 388 (the `kPostLoad` thread) for the hazard; the ticks on 388. |

The `tick` fallback is not needed: `adjust` already rolls once per effect, and a damage-over-time effect deals its scaled magnitude in every tick.

Not covered:

- **The player as attacker:** keyboard attacks work through the bridge, but no NPC was in reach, so neither the player's hits nor the player's crit perks were rolled.
- **Perks that set the chance:** `addperk` from the console works only on the player, so Bladesman and the others were not seen setting the chance; the disassembly shows that they overwrite it, which is why G8 adds after them.
- **A weapon with a CRDT % Mult of 0:** every weapon used had 1.00. Vanilla multiplies the base chance by it, so such a weapon never crits in vanilla; a chance added after the perks is not multiplied.
- **A concentration spell held by the player, Absorb and Dual effects, the value of the spell crit multiplier,** and combat overhauls (none installed).
- **1.6.x:** untested; the sites come from scans.

#### S6a result

Run on 2026-11-28 on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng, through the skyrim-mcp bridge in `qasmoke`, with a staged control map that puts the right and left attack on G and H; no crash log. **Most combat and body actions have a hook that fires once per action with the data the training needs; hits and damage taken fire far more often than once per action, and the social actions were not reached.** The prototype is `modules/stats/plugin/src/TrainingSpike.cpp` and `.h` on the branch `stats/s6a-training-spike` (261b4f4). It patches no code: three vtable slots and event sinks only. That branch stays unmerged, as the reference for G9a to G9c.

Where the actions show up (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE):

- Combat (G9a):
  - Swing: the player's animation graph events `weaponSwing` (right hand) and `weaponLeftSwing` (left hand), through slot 1 of the `PlayerCharacter` vtable of its `BSTEventSink<BSAnimationGraphEvent>` (208044; vanilla `TESObjectREFR::ProcessEvent`, 37997). The names are those of the character behavior files.
  - Bow draw: `bowDrawStart` and `bowDrawn`; `BowRelease` and `arrowRelease` on release. Crossbow load: `reloadStart` and `reloadStop`.
  - Hit: `TESHitEvent` with the player as cause; spells and projectiles come with the spell or weapon as source.
  - Staff use: `TESSpellCastEvent` with a staff enchantment, and the animation events `MLh_SpellFire_Event` and `MRh_SpellFire_Event` with a staff in that hand.
  - Unarmed: a swing or hit without a weapon; the gloves are the armor in the hands slot.
  - Fighting: a once-a-second tick in `PlayerCharacter::Update` (slot 0xAD of 208040, 40447) while the player is in combat; `TESCombatEvent` for context.
  - Spell cast: `TESSpellCastEvent` with the player as caster.
- Body (G9b):
  - Damage taken: `PlayerCharacter::HandleHealthDamage` (slot 0x104 of 208040, 38606), which 38468 calls after Health dropped ([S4 result](#s4-result)).
  - Disease recovery: a disease leaving the player's added spells, polled in the `Update` tick; cure effects through `TESMagicEffectApplyEvent`.
  - Poison resistance: `Actor::CheckResistance` (slot 0xA of the player's `MagicTarget` vtable 208048, 38737). It returns the factor left after `PoisonResist` (actor value 40) for a poison, `ResistMagic` (44) otherwise, times the effect's own resist actor value; 1 means nothing was resisted. The prototype counts an effect as poison when its item is a poison (`SpellType::kPoison`) or the effect's resist actor value is `PoisonResist`.
  - Sprinting with Stamina use and overencumbered walking: the `Update` tick.
- Social (G9c):
  - Trade: `TESContainerChangedEvent` moving gold (form 0xF) to or from the player while the barter menu is open.
  - Persuasion, intimidation, bribe: `TESTrackedStatsEvent` "Persuasions", "Intimidations" and "Bribes", which the vanilla `FavorDialogueScript` sends through `Game.IncrementStat`; the target is the dialogue speaker `MenuTopicManager` holds.
  - Normal interactions: `TESTopicInfoEvent` while the dialogue menu is open, with `MenuOpenCloseEvent` for the dialogue and barter menus.

The prototype logs, per action, the stat XP the shipped default training (`modules/stats/data/actions/_default.json`) would give.

Evidence (plugin log):

| Action | Result |
| --- | --- |
| Install log | The three vtable slots (208044 slot 1, 208040 slots 0xAD and 0x104, 208048 slot 0xA) held the vanilla functions; all sinks registered. |
| Swing (Iron Dagger, key G) | One `weaponSwing` per swing, W = 2 → STR 1.000, as ST-15's example. |
| Bow draw (Hunting Bow) | W = 7 + 7 = 14 → STR 3.147. `bowDrawStart` fired twice for one draw (16 ms apart), `bowDrawn` once; `BowRelease` and `arrowRelease` once each. |
| Staff (Staff of Flames) | `TESSpellCastEvent` with the staff's enchantment and `MRh_SpellFire_Event` at the same moment: W = 8 → INT 2.263. |
| Hit | `TESHitEvent` fired for an arrow, the staff and dagger hits on a forge (furniture) and a static, so it needs a filter to actors. The concentration staff sent a hit about every 0.17 s (7 in 1.1 s) for one use. |
| Fighting | Begin and end lines followed the combat state; one tick per second in combat (PER 0.10 each). |
| Damage taken (a hagraven's spells) | 365 calls of `HandleHealthDamage`, from 40 per Fireball hit down to ticks of 5.4e-06. |
| Disease (the hagraven's Rockjoint) | Logged when contracted and when it left the added spells 2 s later (removed in the run): CON 2.00, END 2.00. |
| Overencumbered walking | One tick per second while moving overencumbered (STR 0.10 each), with the distance moved. |
| Resistance check | `CheckResistance` ran for every effect with its resist actor value (FireResist for Fireball and Firebolt, none for the disease); no poison reached the player. |
| Topic info | Fired outside dialogue for the player's combat barks (topics "Hit", "Attack") and a hagraven's death line, all with the dialogue menu closed. |
| Threads | Animation events, hits, topic info and the resistance check on worker threads 428 to 440; the `Update` tick and `HandleHealthDamage` on 376, the thread of `kPostLoad`; install on 576 (`kDataLoaded`). |

Not covered:

- **Sprinting:** the bridge's Alt chord did not sprint.
- **Poison resistance:** the spider did not engage, so no poison reached the player.
- **Crossbow load, unarmed hits with gloves, a hit on an actor:** not tried or not reached.
- **A spell cast by key:** only the staff's cast was logged through `TESSpellCastEvent`; a player spell was not confirmed separately.
- **The social actions:** the bridge cannot target NPCs, so trade, persuasion, intimidation, bribes and normal interactions inside dialogue were not reached.
- **1.6.x:** untested; nothing is patched, but the vtable IDs and event names are not checked there.

#### S6b result

Run on 2026-11-28 on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng, through the skyrim-mcp bridge in `qasmoke`, in Belethor's shop; no crash log. **Every default check has a site in the game's own formula, and persuasion and intimidation can be changed without an ESP by swapping their condition functions; the game never runs its detection check with the player as the detector, and the dialogue conditions run before the dialogue menu opens.** The prototype is `modules/stats/plugin/src/CheckSpike.cpp` and `.h` on the branch `stats/s6b-checks-spike` (ddb2a4e). That branch stays unmerged, as the reference for G10a to G10c.

Where the checks are computed (disassembly of `SkyrimSE.exe` 1.7.104; Address Library IDs are AE). Each formula also runs its own perk entry (`BGSEntryPoint::HandleEntryPoint`, 23526):

- Lockpicking: the lockpicking menu sets up a lock in 51964 (from 51950). With the player's Lockpicking it calls, once each: 26488, the sweet spot (skill, lock level, broken picks, Mod Lockpick Sweet Spot), at +0x90; 26489, the partial pick angle, at +0xA8; 26490, the pick break, at +0x1AF. Each call follows `movaps xmm0, xmm10` (the skill).
- Pickpocket: 26379 (`AIFormulas::ComputePickpocketSuccess`: thief and target Pickpocket, value, weight, thief, target, detected, item; Mod Pickpocket Chance; clamped to `fPickPocketMin/MaxChance`), called by the steal attempt 40654 at +0x12C (from 51141, which rolls 0 to 99 below the chance) and by the container menu's item card 51130 at +0x213 (the chance shown).
- Sneak and detection: 37774 (from the detection update 42742) calls 26365 once, at +0x5C2, with the detector, the target, the detector's Sneak as its perception, the target's Sneak and 21 values on the stack (distance, light, line of sight, sneaking, movement, combat, and out pointers for the sound, visibility and skill terms). For the player as target, 26365 turns the skill into `fPlayerDetectionSneakMult × skill + fPlayerDetectionSneakBase`, runs Mod Detection Sneak Skill and Mod Detection Movement, and returns the detection level. 26366 (sound events) is called twice from 37774 and not hooked.
- Persuasion: vanilla dialogue uses the condition `GetActorValue Speechcraft` (function index 14, condition function 21416) on the player against the globals `SpeechEasy`, `SpeechAverage` and `SpeechHard`. Intimidation uses `GetIntimidateSuccess` (index 655, condition function 21735), which calls the formula 37500 (player and target Speech, `fIntimidateSpeechcraftCurve`, both levels, Mod Player Intimidation, the target's confidence). `TESConditionItem::IsTrue` (29924) calls a condition function through the script function table (361120; entries of 0x50 bytes, the condition function at +0x40), so a condition is hooked by replacing that pointer, in the game's writable data.
- Barter: the barter menu (vtable 214926, slot 4, 50945) calls 26464 at +0x7B3 when it opens (Speech, out buy and sell multipliers; `fBarterMin/Max`, `fBarterBuyMin`, `fBarterSellMax`), then Mod Buy Prices (8) at +0x7EA and Mod Sell Prices (0x3C) at +0x805 with the merchant, clamps both and passes them to the movie's `SetBarterMultipliers`, which prices the items.

The prototype's hooks (a JSON file in `Data/SKSE/Plugins` sets an adjustment per check, all off by default, and is read again when it changes): the call sites above, each found by its register setup before the call (as many matches as expected), and the two condition-function pointers. Each can scale or add to its input or result.

Evidence (plugin log):

| Check | Result |
| --- | --- |
| Install log | All call sites found with vanilla targets: lock setup 51964+0x90, +0xA8, +0x1AF; pickpocket 40654+0x12C and 51130+0x213; detection 37774+0x5C2; barter 50945+0x7B3, +0x7EA, +0x805. Both condition functions in 361120 vanilla and swapped. No entry detoured. |
| Detection, far (Belethor, Sneak 22; the player sneaking, Sneak 15) | 1398 units, no line of sight: level −20 (sound 0, visibility 0, skill term −5.86). |
| Detection, close | 36 units, line of sight: level 251 (sound 194.33, visibility 69.09, skill term 2.69). |
| The player's Sneak +200 | At 42 units: skill term −37.37, level 209, so the Sneak input works. Not sneaking at 29 units: skill term 10.75, level 259. |
| Who detects whom | 42 calls of 26365, all with the player as target, none with the player as detector. |
| Activating Belethor | `GetActorValue Speechcraft` on the player (20) once and `GetIntimidateSuccess` on Belethor four times (player Speech 20 at level 1 against Speech 27 at level 4, Confidence 2: 0), all before the dialogue menu opened. |
| Threads | Detection and the conditions on worker threads 428 to 440; install on 576 (`kDataLoaded`). |

Not covered (the bridge cannot target containers, locks or NPCs, nor pick dialogue options):

- **Lockpicking:** the sweet spot, the partial pick angle and a pick break.
- **Pickpocket:** the item card's chance and the attempt.
- **Barter:** the prices in the menu and a real transaction.
- **Persuasion and intimidation:** a real dialogue option with the condition, and whether the dialogue menu's state, the speaker or the condition's subject can tell those calls apart from the evaluation at activation.
- **1.6.x:** untested; the sites come from scans and the table index from the game's own table.

#### S7 result

Run on 2026-11-27 and 2026-11-28 on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng (the CSF skill `acrobatics`, `useMult` 1, `useOffset` 0), through the skyrim-mcp bridge in `qasmoke`. **CSF reports its level-ups through its SKSE interface, vanilla `SkillIncrease::Event` fires above 100, and multiplying the XP right after CSF's own Mod Skill Use perk entry stacks multiplicatively with the perks, for uses only.** The prototype is `modules/stats/plugin/src/CsfSkillSpike.cpp` and `.h` on the branch `stats/s7-csf-skill-spike` (14658ec), with the trampoline raised from 28 to 56 bytes in `main.cpp` and a second `SKSE::Trampoline` of 0x100 bytes allocated near `CustomSkills.dll`. That branch stays unmerged, as the reference for G4 and G6.

How CSF 3.2.0 levels its skills (its MIT source, tag v3.2.0, and a disassembly of `CustomSkills.dll` with its PDB; RVAs are of that build, Address Library IDs are AE):

- **Interface:** at its `kDataLoaded`, CSF sends message type 0 from the sender `"CustomSkills"` to its SKSE listeners. The data is its interface, version 1 (0x28 bytes): `ShowStatsMenu`, `AdvanceSkill(id, magnitude)`, `IncrementSkill(id, count)` and `GetEventDispatcher(id)`. Dispatcher 0 is a `BSTEventSource<SkillIncreaseEvent>` whose event carries only `const char* skillID`, no level. The Papyrus side has `OnCustomSkillIncrease(string)` and `CustomSkills.AdvanceSkill`, `IncrementSkill(By)` and `GetSkillLevel`. CSF never sends the vanilla `SkillIncrease::Event`.
- **A CSF skill** is not an engine skill. Its level and progress are two `TESGlobal`s, the level global being the `level` form of the skill's CSF JSON config (Beyond Mastery reads it from there). The `Skill` object holds its ID (an MSVC `std::string`) at +0, the level global at +0x40, the progress global at +0x48 and its info at +0x68.
- **All CSF skill XP** goes through `CustomSkills::Skill::Advance(float magnitude, TESForm*, bool isSkillUse, bool hide)` at RVA 0x4CAE0, never through 40488 or 41561. So the S3 hook never sees CSF XP, and the CSF hook never sees vanilla XP. In `Advance`, in order:
  1. for a use, XP = `magnitude × useMult + useOffset`, a call of `fmaf` at +0xB4 (Beyond Mastery patches it when `bScaleXPGainAbove100` is set);
  2. +0xF8 to +0x143 set the player's `advanceSkill` to none;
  3. `mov ecx, 0x16` at +0x156, then at +0x15B a 6-byte indirect call (`FF 15`) through CSF's own pointer to `BGSEntryPoint::HandleEntryPoint` (23526), entry point Mod Skill Use, on the address of the XP;
  4. the level loop, with the level limit 100 hard-coded at +0x17B and +0x382 (`cmp esi, 100`), and the same in `Skill::Increment`. Beyond Mastery patches them for the skills it enables (`bEnable = true`; `acrobatics` in the test install).
  5. If the level rose, one `SkillIncreaseEvent`, however many levels the call crossed. `Skill::Increment` calls `Advance` once per level.
- **The perk entry site** is an indirect call, so the hook needs a trampoline within reach of `CustomSkills.dll`. CSF's `ReadSkillBook` calls through the same pointer (entry point 5), so the hook patches the call instruction, not the pointer. At +0x15B, r14 holds the `Skill*`. `isSkillUse` is in no register there, so a use is told from a grant by `Advance`'s callers. CSF 3.2.0 has five direct callers: 0x15394 (interface `AdvanceSkill`, a use), 0x30E41 (`UseWorkbench`, a use), 0x319F3 (`SkillUse::UseSkill`, CSF's vtable 0xF7 hook, a use), 0x4EC0A (Papyrus `AdvanceSkill`, a use) and 0x4D35B (`Skill::Increment`: trainers and books, `isSkillUse` false). CSF's `UseSkill` (`CustomSkills.dll`+0x319A0) handles only skill none with a `CustomSkillAdvance_<id>` keyword and chains everything else to vanilla 40488.
- **Experience** has no CSF code. It caps vanilla skills only (18 + 2 × the character level) by redirecting 40488+0x25 and 41562+0x98.
- **Layout:** the prototype reads CSF's `Skill` through the fixed 3.2.0 offsets and the MSVC `std::string` layout. Beyond Mastery also supports only CSF 3.2.0.

The prototype's hooks and sinks:

1. A `"CustomSkills"` listener, registered at `kPostLoad`, takes the interface and registers a sink on dispatcher 0. Vanilla `SkillIncrease::Event` and `TESTrackedStatsEvent` sinks sit next to it.
2. `csf_perk_entry`: the call at `Advance`+0x15B, through a stub in the second trampoline that passes r14 as the `Skill`. It multiplies the XP after the call.
3. `csf_advance_call`: the five direct calls of `Advance`, each marked use or grant by its caller. It sets the flag that hook 2 reads, and can scale the magnitude for comparison.
4. The S3 `perk_entry` and `xp_routine_call` hooks, to compare both paths in one run.

Evidence (plugin log, console; CSF commands through the CSF interface, run from the SKSE task queue):

| Check | Result |
| --- | --- |
| Install log, both runs | CSF interface version 1 received; registered for CSF's `SkillIncreaseEvent`. `Advance` found at RVA 0x4CAE0 (as in 3.2.0). `csf_perk_entry` hooked at 0x4CAE0+0x15B with r14 as the `Skill`. Five `Advance` calls hooked (four uses, one in `Increment`). No `cmp esi, 100` left (Beyond Mastery patched both). Vtable slot 0xF7 holds `CustomSkills.dll`+0x319A0. Experience redirects 40488+0x25 and 41562+0x98 (to `Experience.dll`+0x56AA). No crash log. |
| CSF `AdvanceSkill("acrobatics", 10)` | 10 XP, perk factor ×1.0. Level 0 to 3, with one CSF `SkillIncreaseEvent` for all three levels. |
| + Well Rested (000FB984) | 11 XP (×1.10). |
| + Warrior Stone (000E5F4C) | Still ×1.10: the stone's condition on the advanced skill does not match, since CSF sets `advanceSkill` to none. |
| Factor ×2 at `csf_perk_entry` | 10 XP, 11 after the perks, 22 added. |
| Factor ×2 at `csf_advance_call` | Magnitude 10 to 20, 22 after the perks. |
| CSF `IncrementSkill("acrobatics", 1)` (whole level) | Logged as a grant; 1.067 XP, 1.174 after the perks (CSF runs the perk entry for grants too), the factor not applied. Level 5 to 6, one CSF event. |
| CSF at the cap: `set SkyrimAcrobaticsLevel to 99`, then `AdvanceSkill` 90000 | Level 99 to 100, one CSF event. A second `AdvanceSkill` at 100: no level change, no event. |
| Vanilla at 100: `player.setav onehanded 99`, `player.advskill onehanded 10000` | One `SkillIncrease::Event`, `TESTrackedStatsEvent` "Skill Increases" = 100. The base actor value went 99 to 100, while the prototype's read of the level in `PlayerSkills` stayed 25, so G4 reads the base actor value. |
| Vanilla above 100: One-Handed's cap raised to 110 by Beyond Mastery's Legendary, Upgrade (one perk point, ten dragon souls), `advskill onehanded 10000` | One `SkillIncrease::Event` per level, base actor value 101, 102, 103, each with a `TESTrackedStatsEvent`. |
| Experience cap: `player.setlevel 1`, `advskill onehanded 10` at 103 | The routine ran no perk entry and added no XP. |
| Threads | Install on the `kDataLoaded` thread (588, 576); bridge console commands on 440 and 380; CSF interface commands from the task queue on 376 (assumption 15). |

Both CSF hooks stack multiplicatively with the perks. `csf_advance_call` scales the magnitude before `useOffset` is added, so it is purely multiplicative only for a skill whose `useOffset` is 0, as `acrobatics`' is. `csf_perk_entry` scales the whole XP after the perks, as the S3 hook does, which is why G6 uses it.

The bridge cannot open the console menu, so the prototype ran its commands when any menu closed (the TweenMenu, through Tab).

Not covered:

- **CSF skills above 100:** the test skill's CSF config has `"legendary": null`, so neither CSF nor Beyond Mastery offers Legendary or Upgrade for it.
- **A real in-game CSF use:** skyrim-acrobatics-ng's DLL has no reference to `CustomSkills`, so its jumps probably bypass `Advance`; not verified.
- **CSF versions other than 3.2.0** and **1.6.x**.
- **A real vanilla skill use:** the bridge cannot cast or attack (as in the S3 result).

#### S8 result

Run on 2026-11-28: the build on the dev host, the game on 1.7.104 with Experience, CSF 3.2.0, Beyond Mastery, SkyUI and skyrim-acrobatics-ng, through the skyrim-mcp bridge in `qasmoke`; no crash log. **The Creation Kit compiler runs under Wine 10.0 with Wine Mono 11.3.0 from a CMake custom command, and the plugin binds and calls native functions of the compiled script.** The prototype is on the branch `stats/s8-papyrus-wine-spike` (a2aa572): `cmake/papyrus.cmake`, `cmake/papyrus_compile.cmake`, the `TOM_BUILD_PAPYRUS` option in the top-level `CMakeLists.txt`, a few README lines, `modules/stats/papyrus/TrialsOfMasteryStatsSpike.psc` and `.yaml`, and `modules/stats/plugin/src/PapyrusSpike.cpp` and `.h`. That branch stays unmerged, as the reference for A2.

The build:

- `option(TOM_BUILD_PAPYRUS ... OFF)` includes `cmake/papyrus.cmake`, with the cache variables `TOM_SKYRIM_DIR` (the Skyrim install with the Creation Kit), `TOM_WINE` (found with `find_program`) and `TOM_WINEPREFIX` (empty: Wine's default prefix). It fails at configure time, with the reason, without Wine, without the compiler (`<Skyrim>/Papyrus Compiler/PapyrusCompiler.exe`) or without `TESV_Papyrus_Flags.flg` in `<Skyrim>/Data/Source/Scripts` (D5).
- `tom_add_papyrus_scripts(<target> OUTPUT_DIR <dir> SOURCES <file.psc>...)` adds a target in `ALL` with one custom command per script. Imports are the sources' own folders, then the vanilla sources; paths go to the compiler on Wine's `Z:` drive. The Stats module adds `tom_stats_papyrus`, writing to `build/<preset>/modules/stats/Scripts/`.
- `cmake/papyrus_compile.cmake` runs the compiler for one script with `cmake -P`. The compiler exits with 0 even when compilation fails, so the wrapper removes the old `.pex` first and fails when no new one exists, printing Wine's output only then.
- Natives: the plugin registers them through the SKSE Papyrus interface, which calls it at VM init, with `IVirtualMachine::RegisterFunction` and its default `a_callableFromTasklets` false. It then calls them from `kPostLoadGame` with `DispatchStaticCall` and a callback that logs the result.

Evidence (build output, plugin log):

| Check | Result |
| --- | --- |
| Wine and Wine Mono | Wine 10.0 with Wine Mono 11.3.0 in `~/.wine` (the user action of the S8 row, done). A fresh prefix needs `wine msiexec /i wine-mono-11.3.0-x86.msi` (about 1.2 GB). |
| Compile | About 7 to 13 s per script on a cold Wine. A rebuild with nothing changed does nothing; touching the source rebuilds it; a syntax error fails the build with the compiler's message. |
| CI | Not possible without the Creation Kit's files; with the option OFF, CI is unchanged and green. |
| The `.pex` | Not byte-identical between two builds. Its header holds the Linux user name and host name and the compile time. |
| Natives in game | Registered at VM init (thread 548, before `kDataLoaded`). At `kPostLoadGame` (thread 376): "SpikeAnswer called, returns 42", "returned 42 to the plugin", "SpikeLog called", all on thread 436. |
| Console | A ConsoleUtil SSE NG custom command file (`TrialsOfMasteryStatsSpike.yaml`, `tomspike answer`) installed in `Data/SKSE/CustomConsole` gave "Script command not found" through the bridge, as CSF's own `csf` command did; the bridge's console does not run custom commands, so it is untested, not failed. |

Not covered:

- **A Papyrus script calling the natives** (only the plugin's `DispatchStaticCall`), and Papyrus events.
- **ConsoleUtil's custom commands in the real console:** a manual check for the user. The plan's `cgf` console command is not in Skyrim SE, so A2 needs another console route; this is one option.
- **A clean `.pex` for release:** the header's user name, host name and time stay in every build (privacy).

### Phase 4: Game layer (`modules/stats/plugin/`)

#### G1 Configuration loading in game

- **Goal:** load the INI and the four tables at `kDataLoaded`, with a `PluginQuery` backed by the game's loaded-plugin list.
- **Covers:** ST-3, ST-27, ST-28 (the table is loaded here; K1 acts on it).
- **Interfaces:** uses C2, C3, C7. A game `PluginQuery` adapter, and the editor IDs of all loaded `RE::TESRace` forms for `buildTables` (C3).
  - Builds the one `StatsService` at `kDataLoaded`, in this order (assumption 14): merge the JSON layers with the API-registered layer, `buildTables`, construct the service, then `registerStat` for each API-registered stat with its free-point setting. Until A1 exists, the registered layer is empty.
  - Sets the thread rule of assumption 15 and writes it into the plugin code where the service is held, so the hook tasks, A1 and A2 can follow it.
- **Verification:** `log`. The log lists file order, conflicts, same-name warnings, skipped files and each `TODO(OQ-n)` placeholder in use. A plain test patch and a mod patch in `plugins/` for an inactive plugin behave as C2 specifies.
- **Depends on:** T1.2, C3, C7.
- **Status:** merged (#57).

#### G2 Co-save persistence and actor lifecycle

- **Goal:** store and restore the service state through the SKSE serialization interface, including revert on new game or load.
- **Covers:** ST-3 (race adjustment and default base at load), ST-4, [§8](PRD.md#8-constraints), success criterion 3.
- **Interfaces:** SKSE serialization callbacks (save, load, revert) with a unique ID and record types. The `ActorIdResolver` uses the SKSE FormID resolution. NPC defaults are the default base plus the race adjustment until the API sets values (**placeholder, depends on OQ-1**).
  - Base adjustments are persisted per key ([§8](PRD.md#8-constraints)). After a load, the race key of each actor is set again from the stats table (assumption 11), and so is the player's when the race changes; a race whose editor ID has no entry, or whose entry C3 ignored, gets no adjustment. Next to it, `applyDefaultBase` (C4) applies each stat's current default base, so a patch that changed it moves the base by the difference ([ST-3](PRD.md#71-stats)). A new game's `initStat` with a race adjustment changes the pools from creation on (ST-10); G3 applies those deltas. Which game event reports a race change is to be found in this task.
  - Service calls: after a load, C8's `restore`, then `setRace` for every actor with its current race and `applyDefaultBases` (C8); on the player's race change, `setRace`; on revert, `resetSaveState` (C8). `setRace` exists since C7; `applyDefaultBases`, `resetSaveState` and the accessors `restore` needs come with C8.
- **Verification:**
  - `in-game`: values survive save, quit and load, including a base adjustment set through the API; a new game resets them; going from a save without the plugin and back does not crash; a patch that changes the player's race adjustment moves the base and the pools by the difference at the next load; a patch that changes a stat's default base moves the base and leaves the pools unchanged (at multiplier 1, assumption 13); a new character of a race with an adjustment starts with pools changed by `curve(adjustment) − curve(0)`.
  - `log`: the record versions and the actor count are logged at load.
- **Depends on:** C8, G1.

#### G3 Actor-value effect application

- **Goal:** apply `AvDelta`s to the actor's permanent modifier whenever stats change, for the player and for NPCs with values set through the API.
- **Covers:** ST-10, ST-2 (effects from the stats file for registered stats).
- **Interfaces:** an `AvApplier` that turns `AvDelta` into an actor-value mod on the permanent modifier.
- **Verification:**
  - `in-game`: raising CON changes max Health by the computed delta only; race base values and an active Fortify Health stay; save and load do not double-apply.
  - `log`: each delta is logged.
- **Depends on:** G2. Placeholder coefficients depend on OQ-2.

#### G4 Skill level-up training

- **Goal:** turn skill level-ups into stat XP.
- **Covers:** ST-14, ST-16, ST-17 (stat XP per skill).
- **Interfaces:** two sinks calling `StatsService::trainSkillLevelUp` (C7a), from the [S7 result](#s7-result). Both record which thread the event arrives on and follow G1's thread rule (assumption 15).
  - Vanilla skills: a `SkillIncrease::Event` sink. The event fires once per level, also above 100 with Beyond Mastery. The level reached is the skill's base actor value, not the level field of `PlayerSkills`, which the S7 run showed unchanged.
  - CSF skills: a listener for the sender `"CustomSkills"`, registered at `kPostLoad`, takes CSF's interface (version 1; another version is logged and the CSF path stays off) and registers a sink on dispatcher 0, `SkillIncreaseEvent`. The event carries only the skill ID and is sent once per `Advance` call, which can cross several levels. ST-14 gives stat XP equal to each level reached, so the sink calls `trainSkillLevelUp` once for each level after the one the skill had before that `Advance` call, up to its level now.
  - The level before comes from the `Advance` caller hooks, which G4 installs: the five direct calls of `Advance` in CSF 3.2.0 ([S7 result](#s7-result)), each replaced by a call that records the skill's level (its level global, `Skill`+0x40) and whether the caller is a use, then calls `Advance`. A level set without skill XP (console `set`, another mod writing the global) runs no `Advance`, so it is never counted, as [ST-16](PRD.md#74-training-and-actions) requires. A comparison of the levels before and after the call inside the caller hook, without the event, is an equivalent. A `SkillIncreaseEvent` outside a hooked call trains nothing and is logged.
  - The caller hooks own the CSF version check that G6 reuses: `Advance` and its callers are found by scanning `CustomSkills.dll` and compared with the 3.2.0 RVAs, and the `Skill` offsets (ID at +0 as an MSVC `std::string`, level global at +0x40) are used only when they match. Anything else is logged and the CSF path stays off. They are written into the second trampoline near `CustomSkills.dll` (see G6), which G4 allocates.
  - The CSF skill key comes from one helper, by [§7.8](PRD.md#78-configuration) (assumption 9). It needs the CSF config file that defines each skill; G4 finds where CSF reads those files.
- **Verification:** `in-game` plus `log`:
  - `advskill` on One-Handed logs the split; an unmapped skill gives nothing;
  - One-Handed above 100 (Beyond Mastery's Legendary, Upgrade, as in the S7 run) trains once per level with the base actor value as the level;
  - CSF `AdvanceSkill("acrobatics", 10)` from level 0 trains levels 1, 2 and 3 once each, with a mapping registered under `skyrimacrobatics:acrobatics`; `IncrementSkill` trains the level reached;
  - `set SkyrimAcrobaticsLevel to 50` trains nothing, and the next `AdvanceSkill` that reaches 51 trains level 51 only;
  - the install log names the CSF interface version, the dispatcher sink and the five `Advance` callers (four uses, one grant).
- **Depends on:** G1, C7a. S7 is done ([S7 result](#s7-result)). The split depends on OQ-3.

#### G5 Level-up handling

- **Goal:** react to every player level-up, grant free points, auto-advance, suppress the vanilla reward and open the stat box at the moment found by S1. Tell real level-ups apart from level changes that bypass the level-up (ST-18).
- **Covers:** ST-6 (auto-advance in game), ST-18, ST-19, ST-20, ST-23, success criterion 1.
- **Interfaces:** the hook from the [S1 result](#s1-result), which replaces the LevelUp menu and its tutorial. Each call of its replacement function is one level-up: it calls `onPlayerLevelUp` with the level the skills menu just set, and asks U1 to open the box. Several pending level-ups run in consecutive menu updates, so G5 collects them and opens one box after the last. The perk point is added right after the replaced call, so it is not yet in `perkCount` inside it.
  - Only the hook's replacement function counts as a level-up. `LevelIncrease::Event` is not used to count level-ups, since `UpdateLevel` sends it without a perk point; `setlevel` and `AdvancePCLevel` send nothing. These bypassed changes call nothing in `StatsService` ([S1 result](#s1-result), ST-18).
- **Verification:** `in-game` for:
  - success criterion 1, with vanilla leveling and with Experience;
  - the perk point arriving right away;
  - `player.setlevel` to a higher level that is a multiple of N grants no free points, no perk point and no advance, and the next real level-up grants the points of one level;
  - advancing at multiples of N;
  - no vanilla menu and no "Leveling Up" tutorial, with and without a level-up menu mod.
- **Depends on:** S1, G2. U1 is needed for the box, but G5 can be checked by log first.

#### G6 Learning speed and skill XP modifiers

- **Goal:** multiply the skill XP of a skill use, for vanilla and CSF skills, by the capped learning bonus from C5 and the per-skill modifiers, on top of the other skill XP bonuses, as ST-12 and ST-17 define. A whole level granted at once (skill book, trainer) stays as the game gives it (ST-12).
- **Covers:** ST-12, ST-17 (skill XP).
- **Interfaces:** the hook from the [S3 result](#s3-result), without the prototype's `use_skill` hook:
  - Replaces the call to `BGSEntryPoint::HandleEntryPoint` (23526) at 41561+0x158 (Mod Skill Use, 0x16). The replacement calls 23526, then multiplies the XP it was passed by `(1 + learningBonus) × skillXpFactor(skill)` (C7, C7a). So the factor applies after the game's perks and whatever `useOffset` the skill has.
  - Applies the factor only inside a skill use: a thread-local flag set around the call at 40488+0x25, which calls the replaced call's current target, since Experience redirects it. A whole-level grant through 41562 runs 41561 without the flag and keeps its XP. An equivalent way to tell a use from a grant is fine, if `incpcs` stays unchanged.
  - Finds each site by its scan from the S3 result (exactly one match). If a site is not found, for example because another plugin patched it, nothing is patched and the log says why; the learning bonus is then off. *User decision (2026-11-27):* this is logged only, with no warning on screen. It chains with Experience and CSF as in the S3 result, and the trampoline grows by the two calls.
  - Needs the factor inside the game's XP computation, so it follows G1's thread rule for values needed at once (assumption 15). The hook logs the thread it runs on.
  - CSF custom skills: *user decision (2026-11-27):* they get the learning bonus too (ST-12, "every skill"). Their XP goes through CSF's own `Skill::Advance`, not 41561, so G6 adds a second hook from the [S7 result](#s7-result):
    - Replaces the 6-byte indirect call at `Advance`+0x15B (Mod Skill Use, after `mov ecx, 0x16`) with a jump to a stub in a second trampoline allocated near `CustomSkills.dll` (0x100 bytes in the prototype), since the main one is out of reach of a 32-bit displacement there. The stub passes r14, the `Skill*`, to the replacement, which calls 23526 through CSF's pointer, then multiplies the XP by `(1 + learningBonus) × skillXpFactor(skill)` with the CSF skill's key (assumption 9). The hook patches the call, not CSF's pointer, which `ReadSkillBook` also uses.
    - Applies the factor only for uses: G4's `Advance` caller hooks mark a call from a caller that passes `isSkillUse` true (interface and Papyrus `AdvanceSkill`, `UseWorkbench`, `SkillUse::UseSkill`), not one from `Skill::Increment`. An unknown caller of `Advance` is not hooked, so its XP gets no factor, and the log names it.
    - Version check: G4's check, extended by the +0x15B site. If either fails, this hook is logged and skipped, as for the vanilla sites; the CSF learning bonus is then off.
  - Open technical points, not user decisions: the thread the hooks run on (G1's rule, assumption 15; the S7 run saw the hooks on the thread of the command), a vanilla skill above 100 with Beyond Mastery, a CSF skill above 100, and the 1.6.x byte patterns ([S3 result](#s3-result), [S7 result](#s7-result)). At Experience's cap the vanilla routine runs no perk entry and adds no XP (S7), so the hook does nothing there.
- **Verification:** `in-game` plus `log`, with One-Handed at 63 XP for `player.advskill onehanded 10` (S3 result):
  - the logged XP gain is 63 × the expected factor;
  - with the Warrior Stone and Well Rested, it is 63 × 1.32 × the factor; Alteration with both gives 33 × the factor (×1.10 from the perks, since the stone does not cover it);
  - with the stats at the curve limit, the stat factor is ×3 at most, and the perks still apply on top;
  - `player.incpcs onehanded` gives what the game gives (×1.32 with both perks), without the factor;
  - the install log names both sites, the CSF site and the five `Advance` callers (four uses, one grant), with Experience, CSF 3.2.0 and Beyond Mastery loaded;
  - CSF `AdvanceSkill("acrobatics", 10)` (`useMult` 1, `useOffset` 0) gives 10 × the factor; with Well Rested 11 × the factor; with the Warrior Stone added still 11 × the factor, since the stone does not cover CSF skills (S7 result);
  - CSF `IncrementSkill("acrobatics", 1)` gives what CSF gives (×1.10 with Well Rested), without the factor;
  - a skill at Experience's cap and one above 100 with Beyond Mastery get the factor or stay capped as the game decides, without a crash;
  - a real skill use (a spell cast or a hit), checked by the user by hand if the bridge still cannot cast, gives the same factor as `advskill`.
- **Depends on:** G1 and C7a; G4 for the CSF part (its `Advance` caller hooks, version check and second trampoline). S3, S7 (CSF site) and PR #52 (ST-12 limited to skill uses) are done. The curve depends on OQ-4.

#### G7 END damage redirection

- **Goal:** redirect the END share of incoming damage into Stamina or Magicka per the C5 split.
- **Covers:** ST-11 (damage redirection only; END's other effects of ST-11 are C5b, G7a and G7b).
- **Interfaces:** the hooks of the [S4 result](#s4-result), with the user decisions of PR #55 (ST-11):
  - Replaces the call of 38468 at 37335+0x151 (`DoDamage`'s Health change, after the difficulty multiplier) and splits there, so the share is taken from what the actor would take. Every call is split on its own, so damage over time is split per tick.
  - Tells the kind apart as the prototype does: a thread-local mark set around the call of 37335 at 38586+0x4A7 (physical, with the `HitData`) and at 35086+0x232 (magic, with the effect), cleared after the call. A `DoDamage` call without a mark (fall damage 37998+0xC7, drowning 37348, any other caller) is not redirected (ST-11: damage with no type).
  - Poison is not redirected (ST-11): a magic mark whose effect the game resists with Resist Poison counts as no mark. The test is the one `Actor::CheckResistance` (38737) applies ([S6a result](#s6a-result)): the magic item is a poison (`SpellType::kPoison`), or the effect's resist actor value is `PoisonResist`. G7 checks both cases in game (a weapon poison and a creature poison) before relying on the test.
  - The split: `taken = min(share × damage, pool)`, taken off Stamina (physical) or Magicka (magic) with `DamageActorValue`, and `damage − taken` passed on to Health. The share is `damageRedirectionShare` (C7) of the damaged actor; G7 splits for every actor the service has stats for, as G3 applies effects, where the prototype split only the player.
  - Finds each site by its scan from the S4 result (exactly one match), and checks that the call goes to the vanilla target. If a site is not found, nothing is patched, the log says why, and the redirection is off for the session, with no message box ([D9](#decisions-taken)).
  - Needs the share inside the game's damage computation, on the thread of the hit (workers 436 to 448 in S4) or of the effect update (380, the `kPostLoad` thread), so it follows G1's thread rule for values needed at once (assumption 15). The hook logs its thread.
  - The trampoline grows by the three calls and the two stubs (76 bytes in the prototype).
  - Open technical points, not user decisions: a lethal split hit, a share that leaves no Health damage (whether `HandleHealthDamage` still runs, which G9b's damage taken uses), projectile hits, Dual and Absorb effects, combat overhauls and the 1.6.x byte patterns ([S4 result](#s4-result)).
- **Verification:** `in-game` plus `log`, at a share set through a test patch of the stats file:
  - with full, partial and empty pools, the logged split matches C5, as in the S4 result (a bandit's 7.50 at share 0.5: 3.75 and 3.75; with 4.97 Stamina left, 4.97 taken and the rest to Health; with `staminarate` 0 and no Stamina, all to Health);
  - a Firebolt is split per tick into Magicka, and the logged ticks sum to the damage the effect dealt;
  - fall damage (`setpos z` +1000) and console `damageav` are not split;
  - at a difficulty other than Adept, the logged damage is after the difficulty multiplier and the split follows it;
  - manual checks, run by the user by hand, since the bridge could not bring them about in S4: a frostbite spider's bite, a weapon poison applied by an NPC and a poison trap are not redirected; drowning is not redirected; a split hit that kills the player kills as usual; an arrow hit is split as physical.
- **Depends on:** G3, G1. S4 is done ([S4 result](#s4-result)). The share depends on OQ-6.

#### G7a END fall damage mitigation

- **Goal:** lower the fall damage an actor takes by its fall damage mitigation (C5b).
- **Covers:** ST-11 (fall damage mitigation).
- **Interfaces:** the hook S10 finds. The candidate from the [S4 result](#s4-result) is the call of `DoDamage` at 37998+0xC7, where fall damage arrives with no attacker: the replacement scales the damage by the actor's `fallDamageMitigation` (C5b) and calls the original target. Fall damage is not redirected (ST-11, G7), so G7 leaves it alone; G7a changes only its amount. It finds its site by a scan, logs it and stays off for the session if the site is not found ([D9](#decisions-taken)), and follows G1's thread rule for values needed at once (assumption 15; S4 saw fall damage on a worker thread).
- **Verification:** `in-game` plus `log`: a fall from `setpos z` +1000 logs the damage before and after the mitigation, and Health drops by the mitigated amount (S4: 118.967 unmitigated at Adept); with END at its default base the damage is unchanged; with skyrim-acrobatics-ng loaded, the result S10 records holds. Manual check, run by the user by hand: a real jump from a height.
- **Depends on:** S10, C5b, G1. The curve and the value's unit depend on OQ-11.

#### G7b END breath duration

- **Goal:** change how long an actor can hold its breath before drowning by its breath duration (C5b).
- **Covers:** ST-11 (breath duration).
- **Interfaces:** the hook or value S10 finds for the time under water; drowning damage (37348) is not redirected (G7). It reads `breathDuration` (C5b), finds its site the way S10 describes, logs it and stays off for the session if it is not found ([D9](#decisions-taken)), and follows G1's thread rule (assumption 15).
- **Verification:** `in-game` plus `log`: under water, the time until the first drowning damage changes with END as the curve says and is the vanilla time at the default base; it resets on surfacing. Manual check, run by the user by hand if the bridge cannot dive: swimming under water until drowning starts, with a low and a high END.
- **Depends on:** S10, C5b, G1. The curve and the value's unit depend on OQ-11.

#### G8 DEX crit chance

- **Goal:** apply the DEX base crit chance to physical and magic attacks, with the additive shift slot for other modules.
- **Covers:** ST-13.
- **Notes:**
  - The default crit effects are absolute (C5a), so the chance follows the effective stat, not its distance to the default base.
  - Physical crits, from the [S5 result](#s5-result): replaces the call of the target's Mod Enemy Critical Hit Chance (0x11) perk entry at 44016+0x2CB. The replacement calls the perk entry, then adds `critChance(attacker, Physical)` (C7, with the shifts of other modules) to the chance it was passed, so the base crit chance comes after the game's crit perks, which set the chance (ST-13, PR #55). The added chance is scaled by the weapon's crit multiplier as [ST-13](PRD.md#73-effects) defines: G8 multiplies it by the weapon's CRDT % Mult, read the way 26441 reads it (taken as is when not negative), so a weapon with a multiplier of 0 stays without crits. The game's own chance before it (`CriticalChance` × condition × CRDT % Mult, then the perks) stays as it is, and so do the roll and the crit damage. Every hit goes through 44016, melee and projectiles alike.
  - The game rolls the chance in percent at a resolution of 0.1 % (modulo 1000 against chance × 10), and a single hit can run 44016 more than once, each time with its own roll (S5 result). G8 adds to every run, as vanilla's own chance does, and logs each.
  - Spell crits, from the S5 result: replaces slot 0 (`AdjustForPerks`) of the vtables of `ValueModifierEffect` (206143), `AccumulatingValueModifierEffect` (205803), `PeakValueModifierEffect` (206014) and `DualValueModifierEffect` (205876), calling the previous function. After it, an effect that can crit by [ST-13](PRD.md#73-effects) rolls once against `critChance(caster, Magic)`, and on a crit its magnitude is multiplied by the spell crit multiplier from the stats file (ST-13; its default value is part of OQ-2, and its core part is C5b).
  - Which effects roll: ST-13 limits spell crits to Health damage from cast spells and staves. So G8 tells them apart from what the prototype also rolled for (a fire hazard with casting source 3 in S5) and from weapon enchantments, poisons and traps, by the magic item's type (a spell, or the enchantment of a staff in the casting hand) and its casting source (left or right hand, not instant or other). G8 logs every effect it skips with its item type and casting source, and checks the rule in game before relying on it. A damage-over-time effect rolls once and deals the scaled magnitude in every tick; the per-tick fallback at 35086+0x232 is not used.
  - Finds each site by its scan from the S5 result (exactly one match, vanilla target) and logs whose function each vtable slot held. If a site is not found, that part stays off for the session and the log says why, with no message box ([D9](#decisions-taken)).
  - Both hooks need the chance inside the game's computation, on worker threads (428 to 440 in S5) and, for some effects, on the `kPostLoad` thread (388), so they follow G1's thread rule for values needed at once (assumption 15). The crit shifts of other modules run on those threads too, which A1 documents. The hooks log their thread.
  - `critChance` can be infinite or NaN (a curve or a shift from another module). G8 does not hand such a value to the game: it falls back to the base crit chance, or to no crit if that is not finite either, and logs it. Whether to also clamp to [0, 100 %] is for the user.
  - The crit chance's unit (fraction or percent, and how it maps to the game's crit value) is part of OQ-2. G8 does not pick it silently: it is raised with the user before G8 starts, and the placeholder curve's `TODO(OQ-2)` names it.
  - Open technical points, not user decisions: the player as attacker and the crit perks setting the chance (not seen in S5), a concentration spell held by the player, Absorb and Dual effects, and the 1.6.x byte patterns.
- **Verification:**
  - `in-game` plus `log`: the logged crit chance follows the curve, and over many hits the crit rate is close to it.
  - `in-game` plus `log`, steps from the S5 run: NPC-versus-NPC hits (bandits and hagravens) roll the DEX chance after the perks; a Firebolt cast at the player rolls once per effect, and a crit scales its magnitude by the multiplier; a fire hazard does not roll.
  - `unit` or `log`: a shift returning NaN or infinity does not reach the game and is logged.
  - Manual checks, run by the user by hand, since the bridge could not do them in S5: the player's own hits on an NPC crit at the logged rate; with Bladesman (or another crit perk) added through `player.addperk`, the chance is the perk's value plus the DEX chance; a held concentration spell (Flames) cast by the player rolls once per cast; an Absorb Health spell cast at the player is logged with whether it rolled; a weapon enchantment, a weapon poison and a trap never roll; a weapon whose CRDT % Mult is 0, if the test install has one, never crits.
- **Depends on:** G1, C5b (the spell crit multiplier). S5 is done ([S5 result](#s5-result)). The curve and the spell crit multiplier's default depend on OQ-2.

#### G9 Action training hooks

These are three tasks with no shared files, all after G1, each **parallel** with the others. A fourth task, G9d, is not a hook. The hooks are those of the [S6a result](#s6a-result); actions that fire very often train per encounter (ST-15, PR #55).

- **G9a Combat:** swing, bow draw, crossbow load, hit, staves, unarmed and gloves, fighting, spell cast.
  - Swing: `weaponSwing` and `weaponLeftSwing` through slot 1 of vtable 208044, with the weapon in that hand; W is its weight.
  - Bow draw: `bowDrawn`, which fired once per draw in S6a, where `bowDrawStart` fired twice; W is the bow's weight plus its damage. Crossbow load: `reloadStop` (or `reloadStart`, whichever fires once per load; not seen in S6a).
  - Hit: `TESHitEvent` with the player as cause and an actor as target (S6a saw it for furniture and statics). A hit counts once per swing, shot or cast and target (ST-15, PR #55): G9a keeps the targets already hit since the swing, release or cast that started the attack, so a concentration spell or staff that hits every 0.17 s counts once per cast and target. How a hit is matched to its swing, shot or cast (the source, the hand, a projectile in flight while the next is drawn) is an open technical point for G9a.
  - Staff use: `TESSpellCastEvent` with a staff enchantment; W is the staff's weight. `MRh_SpellFire_Event` and `MLh_SpellFire_Event` fire at the same moment and are not used, so a use counts once.
  - Unarmed: a swing without a weapon; the gloves are the armor in the hands slot (OQ-9).
  - Fighting: the 1 s tick in `PlayerCharacter::Update` (slot 0xAD of 208040) while the player is in combat (OQ-10).
  - Spell cast: `TESSpellCastEvent` with the player as caster and a spell, not a staff enchantment.
- **G9b Body:** damage taken, disease recovery, poison resistance, sprinting with stamina use, overencumbered walking.
  - Damage taken: `HandleHealthDamage` (slot 0x104 of 208040), after Health changed. It trains by the damage summed over time (ST-15, PR #55), since S6a saw ticks from 40 down to 5.4e-06: G9b sums the damage and calls `applyTraining("DamageTaken", sum)` once per `Update` tick, so the action's curve takes the amount. The shipped placeholder (a fixed XP per call) becomes an amount curve, still marked `TODO(OQ-7)`.
  - Disease recovery: a disease leaving the player's added spells, polled in the `Update` tick.
  - Poison resistance: `CheckResistance` (slot 0xA of 208048) for a poison as G7 tells it apart (its item is a poison, or its effect's resist actor value is `PoisonResist`), when the factor is below 1.
  - Sprinting with Stamina use and overencumbered walking: the `Update` tick, with the distance moved; a jump above a set distance in one frame is a teleport and does not count.
- **G9c Social:** trade by money value, persuasion and intimidation by target, normal interactions.
  - Trade: `TESContainerChangedEvent` moving gold (form 0xF) to or from the player while the barter menu is open; M is the gold moved.
  - Persuasion and intimidation: `TESTrackedStatsEvent` "Persuasions" and "Intimidations" (and "Bribes", which ST-15 does not list and G9c only logs), with the dialogue speaker as the target.
  - Normal interactions: `TESTopicInfoEvent` only while the dialogue menu is open and with an NPC as the speaker, since it also fires for combat barks and death lines outside dialogue (S6a). What counts as one interaction (one line, one topic, one conversation) is an open technical point for G9c, not seen in S6a.
- **G9d Mod actions:** the shared `applyTraining` entry for API-registered actions (ST-16). It needs no hook. A1 wires it up.
- **Covers:** ST-15 (training), ST-16, ST-17 (stat XP per action).
- **Interfaces:** every action calls `StatsService::applyTraining` (C7a). Each hook records which thread it runs on and follows G1's thread rule (assumption 15): in S6a the animation events, hits and topic info came on worker threads (428 to 440), the `Update` tick and `HandleHealthDamage` on the `kPostLoad` thread (376). Training needs no value at once, so a hook off that thread can defer its call. A hook that cannot be installed (a vtable slot or sink that fails) is logged, and its action trains nothing for the session, with no message box ([D9](#decisions-taken)). G9a and G9b share the `Update` hook of 208040 slot 0xAD; the first of them to merge adds it with a way for the other to subscribe, so the slot is replaced once.
- **Verification:** `in-game` plus `log`. Each action logs its XP, and the ST-15 example values (dagger, Stendarr's Hammer, 100 septims) come out. Per task:
  - G9a: a dagger swing gives STR 1.000 per swing (S6a); a Hunting Bow gives STR 3.147 once per draw; the Staff of Flames gives INT 2.263 per use; hitting a forge or a wall gives no DEX; a concentration staff held on one NPC gives one hit's DEX per use; fighting ticks once per second in combat. Manual checks, run by the user by hand: a crossbow load, an unarmed hit with and without gloves, a hit on an NPC with a weapon and an arrow, a player spell cast by key, and one hit on two NPCs with one swing (two hits, once each).
  - G9b: a hagraven's spells train by the summed damage, not per tick; a disease gives its XP when it leaves; overencumbered walking trains per second while moving and not while standing or after `coc`. Manual checks, run by the user by hand: sprinting trains only while it uses Stamina; a resisted poison (a frostbite spider, with some `PoisonResist`) trains, and an unresisted one does not.
  - G9c: all checks are manual, run by the user by hand, since the bridge cannot target NPCs: buying and selling for 100 septims gives CHA 1 and 1000 septims about 3; a successful persuasion and intimidation train CHA by target; a dialogue line with an NPC trains once per interaction, and combat barks do not.
- **Depends on:** C7a, G1. S6a is done ([S6a result](#s6a-result)). XP amounts depend on OQ-7; G9a's unarmed STR XP by glove weight depends on OQ-9 and its fighting PER XP on OQ-10.

#### G10 Action checks

Three tasks, all after C7a, each **parallel**. Each hook calls `StatsService::checkValue` and `checkResult` (C7a) inside the game's own computation, records which thread it runs on and follows G1's thread rule for values needed at once (assumption 15); in S6b every check ran on worker threads (428 to 440). The sites are those of the [S6b result](#s6b-result), each found by its scan (as many matches as expected, vanilla targets); a site that is not found stays unpatched for the session, and the log says why, with no message box ([D9](#decisions-taken)).

- **G10a:** lockpicking and pickpocket.
  - Lockpicking: the three calls in the lock setup 51964: the sweet spot 26488 at +0x90, the partial pick angle 26489 at +0xA8 and the pick break 26490 at +0x1AF. Which of them the check drives (ST-15 names the sweet spot) is part of the result mapping, OQ-5.
  - Pickpocket: the calls of 26379 in the steal attempt (40654+0x12C) and in the item card (51130+0x213), both, so the chance shown is the chance rolled.
- **G10b:** sneak. The detection check of PER is removed (PR #55): the game never runs its detection check with the player as the detector (S6b), and PER's sneak reveal is a named effect (ST-13a, G11).
  - The call of 26365 at 37774+0x5C2 where the player is the target, with the player's Sneak as the input S6b changed (`+200` took the skill term from 2.69 to −37.37 at the same distance).
  - Removes the `Detection` action from `modules/stats/data/actions/_default.json`, with the unit tests that name it.
- **G10c:** persuasion, intimidation and barter.
  - Persuasion: the condition function of `GetActorValue` (21416) in the script function table (361120), for Speechcraft on the player; intimidation: that of `GetIntimidateSuccess` (21735). Both are swapped in the table, without an ESP. The vanilla functions are called for every other use.
  - Barter: the call of 26464 at 50945+0x7B3, before Mod Buy Prices (+0x7EA) and Mod Sell Prices (+0x805), so the perks still apply on top.
  - The dialogue conditions run when the player activates the NPC, before the dialogue menu opens (S6b), and `GetActorValue Speechcraft` is used by conditions outside persuasion too. So G10c cannot tell a persuasion check by the open menu. Which filter it uses (the speaker of the conversation, the condition's subject and the topic, or the condition's run context) is an open technical point for G10c, to settle in game before it changes a value; until then G10c logs every call it would change.
- **Open, raised with the user before G10 starts (with OQ-5):** each site can take `checkResult` as the formula's skill input, which keeps the game's lock levels, GMSTs and perk entries, or as the formula's result, which replaces them (assumption 7 maps `I` to the game quantity). The S6b prototype tried both; the choice changes what the perks do, so it is not taken here.
- **Covers:** ST-15 (checks).
- **Verification:**
  - `log`: shows `I` and the resulting game value per attempt.
  - `in-game`: raising DEX widens the sweet spot; a novice skill level with a high stat still fails master-level checks.
  - G10b, through the bridge as in S6b: near a merchant while sneaking, the logged detection level changes with DEX and Sneak as the check says, and the log shows no call with the player as detector.
  - Manual checks, run by the user by hand, since the bridge cannot target locks, containers or NPCs: lockpicking (sweet spot, partial pick, a broken pick) on locks of two levels; pickpocket chance in the item card and the attempt; barter prices in the menu and the gold of a real sale and purchase; a persuade and an intimidate option in dialogue, each with a stat high enough and too low, and that activating the NPC alone changes nothing visible.
- **Depends on:** C7a, G1. S6b is done ([S6b result](#s6b-result)). Weights and mappings depend on OQ-5.

#### G11 PER sneak reveal

- **Goal:** give sneaking NPCs within the player's sneak reveal range a visual cue (ST-13a).
- **Covers:** ST-13a.
- **Interfaces:** the cue and the detection of sneaking NPCs that S9 finds and the user picks (OQ-12). It reads `sneakRevealRange` (C5b) of the player, adds the cue when a sneaking NPC comes within the range (and in line of sight, if OQ-12 asks for it) and removes it when the NPC stops sneaking, leaves the range or unloads. It needs no value inside a game computation, so it follows G1's thread rule (assumption 15). Without an ESP ([§8](PRD.md#8-constraints)).
- **Verification:** `in-game` plus `log`: a sneaking NPC (made to sneak the way S9 records) gets the cue inside the range and loses it outside, when it stops sneaking and after a cell change; a non-sneaking NPC never gets it; the range follows PER. Manual check, run by the user by hand: the cue looks as OQ-12 decides, with and without SkyUI.
- **Depends on:** S9, C5b, G1. The cue, the range curve and line of sight depend on OQ-12.

### Phase 5: Stat box UI

#### U1 Stat box

- **Goal:** show the C9 pages as native message boxes. They open by key in the skills menu and from G5.
- **Covers:** ST-21, ST-22, ST-24.
- **Interfaces:**
  - An input sink on `RE::BSInputDeviceManager` that is active only while `RE::StatsMenu` is open (tracked by a `RE::MenuOpenCloseEvent` sink) and uses the INI key and gamepad button, as in the S2 prototype ([S2 result](#s2-result); branch `stats/s2-stat-box-spike`, not merged).
  - A message-box adapter that shows a body text with button labels (a `Page` here, K1's warning later) and returns the pressed index: a `RE::MessageBoxData` from the `MessageDataFactoryManager` creator, with `bodyText`, `buttonText`, `verticalButtons = true` (ST-24, PR #41) and an `RE::IMessageBoxCallback`, queued with `RE::MessageBoxMenu::QueueMessage` (52271). The other fields keep the factory defaults S2 logged, among them `isCancellable` false and `cancelButtonIndex` −1 (assumption 19).
  - Each opening builds a new `StatBoxModel` (C9b) over the service, the `Tables` G1 built (the service takes its own copy, so the plugin keeps one for the box), the plugin's `StatBoxSections` (A1, A2) and a `StatBoxLayout` of `maxButtons` 10 (S2, with vertical buttons) and `Settings::statBoxLinesPerPage`. The adapter applies `PressResult::deltas` through G3 and logs `PressResult::error` and `StatBoxModel::errors()`.
  - `LinesPerPage` keeps its default of 15 (C3b): 15 lines show at full size, and the box stays readable up to about 30 lines, which leaves room for lines that wrap at about 40 characters. U1 drops the placeholder comments in `settings.h` and the shipped INI.
- **Verification:** `in-game`:
  - the key opens the box with and without SkyUI, and does nothing outside the skills menu;
  - manual checks the S2 run could not do (user decision, [S2 result](#s2-result)), run by the user by hand: the gamepad's Y opens the box in the skills menu and does nothing else there; without SkyUI, the box opens over the skills menu and the default overview and a page of 10 buttons fit; the same at a resolution other than 1280×720;
  - an infinite cost shows as `--` (C9d);
  - the overview shows the four default sections with display names in full, with and without SkyUI;
  - every ST-21 flow works, and after the last point the box shows the first page;
  - with `LinesPerPage` lowered in the INI, the overview splits into pages with "Back" and "Next" and a long section shows its parts; with a low button count (a test build), the group list and a group's stats are paged; with and without SkyUI;
  - unspent points survive Cancel and save/load;
  - group pages appear with a registered test stat, and a registered test stat without a group shows in its namespace's section;
  - with a point-assignment claim, only "OK" shows;
  - Escape while the box is open, on the overview with "OK", on the overview with "Level Up" and "Cancel", and on a level-up page; and, by hand (the bridge sends no gamepad input), the gamepad's cancel button (B) on the same pages. For each, record what happens: the box closes (and whether the callback runs, with which index, or not at all), the skills menu closes as well, the skills menu closes behind a box that stays, or nothing; whether the unspent points stay pending; and whether the key opens the box again afterwards. The PRD does not define this (assumption 19), so U1 does not pick a behaviour: it keeps the factory defaults (`isCancellable` false, `cancelButtonIndex` −1), reports the result and asks the user which behaviour is wanted, for example Escape acting as "Cancel" or "OK". A change follows only from the user's answer.
- **Depends on:** S2, C3b, C9c, C9d, G5.

#### U2 Skills-menu hint (Could): dropped for v1

- **Status:** dropped for v1 (user decision, 2026-11-27, D8). No task depends on it, and it is not in the order below. ST-25 stays a Could in the PRD; nothing in v1 implements it.
- **Why:** the only route S2 tried, a HUD message sent when the skills menu opens, is not visible over the skills menu, nor after it closes ([S2 result](#s2-result)). Other routes were not tried.
- **Original scope, kept for a later version:** show the key hint in the skills menu, only if this works without changing that menu (ST-25); `in-game`: the hint is visible, and the skills menu is unchanged with SkyUI and with CSF. It would depend on U1.

### Phase 6: API

#### A1 C++ API

- **Goal:** a versioned public C++ API for other SKSE plugins.
- **Covers:** ST-2, ST-3 (base adjustments), ST-6 (claim advancing, set N), ST-8, ST-13 (crit shifts), ST-16, ST-17, ST-22, ST-24 (section providers), ST-26.
- **Scope:** `modules/stats/include/tom/stats/api.h` (header only, no CommonLib types beyond forward-declared actors) and the plugin-side implementation.
- **Interfaces:**
  - An interface struct with a version number, handed out through the SKSE messaging interface on request.
  - Its functions map one to one onto `StatsService`, except `onPlayerLevelUp`, which G5 alone calls and the API does not expose (ST-18). Event callbacks are registered per event type. The XP modifiers, the training of mod actions (G9d) and the check values are the C7a operations; C7 alone does not have them.
  - Registrations are accepted until `kDataLoaded` and merged per assumption 3. They are not service calls: the service does not exist before `kDataLoaded`, and its tables are fixed once it does (assumption 14). A1 collects each registered table entry, and for a stat its free-point setting (ST-2), and hands them to G1, which merges them before `buildTables` and then calls `registerStat`. So the interface struct must be obtainable before `kDataLoaded`, for example at `kPostPostLoad`, and a registration after `kDataLoaded` fails with an error. Each registered table entry (stat, skill mapping, action) first runs through the C3 validator of its table; an entry invalid in itself (a wrong type, a missing field, an unknown curve) is rejected, the call returns an error and the rejection is logged ([§9](PRD.md#9-risks)). An entry that names an undefined stat is accepted here and dropped by `buildTables` like any other (C3).
  - A registered stat's group, display name and short name come in its stats-table entry (C3a); without a group it is in its namespace's group (ST-21).
  - Setting and removing the stat box section provider of a namespace ([ST-24](PRD.md#76-display), [LU-1](../luck/PRD.md)): a function the box calls when it opens, stored in the plugin's `StatBoxSections` (C9b), not in the service. Like crit shifts it is session state, not saved, and can be set after `kDataLoaded`. A1 decides how the caller shows that it owns the namespace, for example by having registered a stat in it, and documents that the function runs on the thread that opens the box (assumption 15) and must not throw across the DLL boundary.
  - Calls are allowed from any thread, and the header documents this. It also documents G1's thread rule (assumption 15) where it reaches API users, with a link to [Service.h](../../modules/stats/plugin/src/Service.h): a crit shift is called on the thread of G8's hook, and an event callback on the thread that made the change, both under the stats lock. So neither may wait on another thread, and a module must not call the stats API while it holds a lock that its own crit shift or event sink also takes.
  - `ActorStats` carries no actor identity, but a crit shift ([LU-7](../luck/PRD.md)) needs the actor to read its LUC and favour. A1 hands the luck module the actor: either a reverse lookup from `ActorStats` to the actor in the plugin, or an opaque actor view passed to the shift instead of `ActorStats`. Which one is decided in A1; the second changes C5's `CritShift` signature.
- **Verification:** `unit` for the version check and argument validation in the core part, including a registered entry that its C3 validator rejects: the call fails, the rejection is logged and the entry does not reach the merge; a registered skill mapping naming an undefined stat is accepted and later dropped by `buildTables`. A3 does the end-to-end check.
- **Depends on:** C7, C7a, C9b, G1.

#### A2 Papyrus API

- **Goal:** a compiled Papyrus script with native functions and Papyrus events.
- **Covers:** ST-26, assumption 5.
- **Scope:** `modules/stats/papyrus/` (the script source, with the `.pex` as a build output) and the native bindings in the plugin. The build comes from the [S8 result](#s8-result) (branch `stats/s8-papyrus-wine-spike`, not merged) and is part of A2: `cmake/papyrus.cmake` and `cmake/papyrus_compile.cmake` with `tom_add_papyrus_scripts`, the `TOM_BUILD_PAPYRUS` option (OFF by default, so CI, which has no Creation Kit, stays as it is), the cache variables `TOM_SKYRIM_DIR`, `TOM_WINE` and `TOM_WINEPREFIX`, the target `tom_stats_papyrus`, and the README lines for it ([README.md#build-and-test](../../README.md#build-and-test)), including the Wine Mono install of a fresh prefix.
- **Interfaces:**
  - Global native functions that mirror A1, with stats addressed by name, except the crit shift registration, which is C++ only (ST-26).
  - The section of a namespace's group (ST-24, ST-26) is filled with lines: natives set and clear the lines of the caller's namespace, under A1's ownership rule. A2 keeps the lines and registers, per namespace, a `SectionProvider` (C9b) that returns them, so the box reads them when it opens (assumption 17).
  - Ascend takes a curve name and parameters and passes them on to the C++ ascend.
  - Events are delivered as SKSE mod events with documented names and arguments.
  - Natives are registered through the SKSE Papyrus interface at VM init, as in the S8 prototype.
  - Natives that call the service follow G1's thread rule (assumption 15). `RE::BSScript::IVirtualMachine::RegisterFunction` takes `a_callableFromTasklets`, `false` by default, which makes a native wait for the next frame instead of running at once on a script thread; A2 keeps that default for them unless the rule allows otherwise. With that default, the S8 natives still ran on thread 436, not on the main loop's thread (376), so the default alone does not put them on the main thread.
  - The console route for the in-game checks: `cgf` is not in Skyrim SE. One option is a ConsoleUtil SSE NG custom command file (`Data/SKSE/CustomConsole/<name>.yaml`, as the S8 prototype's `TrialsOfMasteryStatsSpike.yaml`), which calls the script's global functions; it was untested in S8, since the bridge's console does not run custom commands. A2 picks the route after the user's manual check below; it is an option here, not a decision.
  - A2 does not ship the `.pex` with the build machine's user and host name in its header; R1 blanks them.
- **Verification:**
  - Build: with `TOM_BUILD_PAPYRUS=ON` and `TOM_SKYRIM_DIR` set, `cmake --build --preset plugin` compiles the script; a second build does nothing; a syntax error fails the build; with the option OFF (CI), nothing about Papyrus runs.
  - `in-game`: console calls through the chosen route read and change values, a test script receives each event, lines set from the console for the namespace of a registered test stat show in its section the next time the box opens and clearing them brings the stat lines back, and `Papyrus.0.log` shows no stack dumps.
  - Manual check, run by the user by hand before A2 picks the console route: with ConsoleUtil SSE NG and the S8 prototype's `TrialsOfMasteryStatsSpike.yaml` installed, `tomspike answer` in the game's console logs "SpikeAnswer called, returns 42".
- **Depends on:** A1. S8 is done ([S8 result](#s8-result)).

#### A3 API consumer test plugin

- **Goal:** a small separate SKSE plugin that runs success criterion 2.
- **Covers:** success criterion 2.
- **Scope:** `modules/stats/tests/consumer/` (a separate DLL target, not shipped).
- **Verification:** `log`. Each step of success criterion 2 is logged with pass or fail.
- **Depends on:** A1, G4, G9d.

### Phase 7: Compatibility and release checks

#### K1 Incompatible-mod detection

- **Goal:** check the entries of the incompatible table at load against the loaded game plugins and SKSE plugins, then log, warn and disable, or run with the INI override; the warning names each detected mod that is not dismissed, and "Don't show again" dismisses them in the INI (PR #43, assumption 18).
- **Covers:** ST-28, ST-27 (writing the dismissed mods back).
- **Scope:** the plugin only (`modules/stats/plugin/src/`); the logic is C10's.
- **Interfaces:**
  - Uses `IncompatibleTable` (C3, loaded by G1), `Settings::dismissedIncompatibleMods` (C3c) and C10's functions. The game adapter turns the loaded game plugins (the same list as G1's `PluginQuery`) and the loaded SKSE plugin DLLs into C10's inputs. The table names SKSE plugins by DLL file name; `SKSE::GetPluginInfo` takes a plugin name, so K1 checks whether that name is the DLL's file name or the name the plugin declares, and if it is the latter, finds the DLLs another way (for example the modules loaded in the process) and says so in the code.
  - Adds a module "disabled" state: hooks stay inert, and the API reports that the module is disabled.
  - At every load, the log lists each detected mod with the files that matched and whether it is dismissed, and whether the module is disabled or runs through the override.
  - The warning: when `warnedMods` (C10) is not empty, a box through U1's message-box adapter with `warningText` and the buttons "OK" and "Don't show again" (`WarningButton`). "Don't show again" writes `withDismissed(...)` back to `trials-of-mastery-stats.ini` as assumption 18 describes, with `formatNameList` (C3c) and `setIniValue` (C10), and logs the new list or the failed write. The dismissal takes effect at the next load; the module stays disabled in this session.
  - With the override `AllowIncompatibleMods` set, the module runs, no box is queued (`warnedMods` is empty), and the log still records the detected mods (ST-28).
  - When the box shows: ST-28 says "at load". K1 queues it once the main menu is open after `kDataLoaded`, if a box queued there shows over the main menu (S2 tested only over the skills menu). If it does not, K1 shows it at the first `kPostLoadGame` or `kNewGame` of the session instead, as ST-28 allows, and records which of the two it uses in the code and in `K1.md`.
- **Verification:**
  - `unit`: the detection, warning content, dismissal and INI write are C10's tests; K1 adds unit tests only for logic it does not take from C10.
  - `in-game` plus `log`, with the shipped INI unless a step says otherwise, steps in `modules/stats/tests/ingame/K1.md`:
    - with a dummy `Requiem.esp`, the log shows the detection, the box names `Requiem` with "OK" and "Don't show again" stacked, and the module stays inert; with the override set, the module runs, no box shows, and the log still lists `Requiem` as detected;
    - with dummy `Requiem.esp` and `ValhallaCombat.esp`, the box names both; "OK" changes nothing in the INI, and the box shows again at the next start;
    - "Don't show again" writes `DismissedWarnings = Requiem, Valhalla Combat`, leaves every other line of the INI as it was (`diff` against the copy from before), and at the next start no box shows, the module is still disabled and the log still lists both mods as dismissed;
    - after removing `Valhalla Combat` from the list by hand, the next start shows a box that names only `Valhalla Combat`;
    - with a read-only INI, "Don't show again" logs the failed write, the box closes and the warning shows again at the next start;
    - a patch file adding an entry is honoured, and its mod can be dismissed like a default one;
    - Escape and, by hand, the gamepad's cancel button (B) while the warning is open: record whether the box closes, whether either counts as a button (and which) and whether the INI changes (assumption 19);
    - with Imperious, Morningstar or Mannaz installed (each where available), no warning shows and the log reports no detection ([§7.9](PRD.md#79-compatibility): none is detected at load).
- **Depends on:** G1, A1 (the disabled state must reach the API), U1 (the message-box adapter), C3c, C10, and PR #45 merged (override and fallback timing in ST-28).

#### R1 Blank the `.pex` header for release

- **Goal:** the shipped `.pex` files carry neither the build machine's user name nor its host name ([S8 result](#s8-result)).
- **Covers:** privacy of the package; no PRD requirement. The plan has no packaging task: how the release archive is built is not planned here, only that its `.pex` files are clean.
- **Scope:** the Papyrus compile wrapper from A2 (`cmake/papyrus_compile.cmake`), or a small step it runs after the compiler.
- **Interfaces:** after a successful compile, the wrapper rewrites the header fields of the `.pex` that name the build machine: the user name and the computer name (fixed placeholder text), the compile time (a fixed value), and the source file name if it holds a path. The field layout comes from the description of the `.pex` format the implementer cites in the code. Whether the result is then byte-identical between builds is recorded; anything else that differs is listed in the task's pull request.
- **Verification:**
  - Build: searching the built `.pex` for the build machine's user name and host name finds neither; two clean builds give the same file, or the remaining differences are listed.
  - `in-game`: the game loads the rewritten script, its natives answer as in A2's checks, and `Papyrus.0.log` shows no error for it.
- **Depends on:** A2.

#### V1 Release test pass

- **Goal:** run success criteria 1 to 3 in one documented session: vanilla leveling, Experience, save and load, and a long play session.
- **Covers:** [§3 success criteria](PRD.md#3-goals-and-success-criteria) 1 to 3.
- **Verification:** `in-game` checklist in `tests/ingame/V1.md`, with no crashes and with clean `Papyrus.0.log` and plugin logs. It also covers the race overhauls [§7.9](PRD.md#79-compatibility) marks "test", one run each: with Morningstar and with Mannaz, a new character of a race they change gets the module's pool contributions on top of the overhaul's own boosts (ST-10), and the stats and the box behave as without them; with Imperious, the module works without the planned patch. The results go into the checklist for the user to judge; balance and tweaks are not part of V1.
- **Depends on:** all tasks above, except U2 (dropped for v1, D8).

### Not in this plan

- **The Imperious patch** ([§7.9](PRD.md#79-compatibility), status Patch, "a later patch"): race adjustments (ST-3) for its races, shipped as a stats patch. The PRD names neither its values nor when it ships, and the plan has no patch tasks, so it is out of v1. It needs no code: race adjustments already come from the stats file, keyed by race editor ID (C3).

### Order and parallelism

- Sequential path: T1.1, then C4 and C1 and C2, then C3, C5, C5a, C6, C7, C8, C7a, G1, G2, G3, G5, U1, A1, A2, A3, K1, V1. U2 is dropped for v1 (D8) and is not in the order.
- Parallel tracks:
  - T1.2 and the spikes run alongside Phase 2. S1 to S6b and S8 are done; S9 and S10 (PR #55) run at any time.
  - The tasks of PR #55: C5b runs after C7a and C8 (shared service files); G7a and G7b follow S10, C5b and G1, and G11 follows S9, C5b and G1, each in parallel with the other G tasks. G7a's site (37998+0xC7) is not one of G7's, so G7a keeps its hook in its own file. R1 follows A2.
  - T1.3 (sanitizer CI) touches only the presets, the workflow, `CONTRIBUTING.md` and the README, and runs at any time.
  - C1, C2 and C4 run in parallel. C9 runs in parallel with C8 and C7a; C8 and C7a share the service files, so they run one after the other, in either order.
  - The stat box amendments of PR #31: C9a only adds files and runs at any time. C3a runs in parallel with C8, C7a and C9a. C9b follows C3a, C9a and the merge of C8 (`stat_value.h`), and runs in parallel with C7a. C3b (PR #33) runs at any time. C9c follows C9b and C3b. C9d (S2) runs at any time once PR #41 is merged. U1 needs C9c and C9d, A1 needs C9b.
  - The compatibility warning (PR #43): C3c runs at any time; C10 follows C3c. Both must merge before K1, which also needs U1.
  - G6, G7, G8, G9a to G9c and G10a to G10c run in parallel once their spike and G1 or G3 are done; S7, which G6 also waited for, is done. G6's CSF hook builds on G4's `Advance` caller hooks, so that part follows G4.

## Risks

- **Game version (T1.2):** testing covers only 1.7.104. Address Library IDs found in the spikes must exist for 1.6.x as well, or the claim "1.6.x and newer" is untested there. Checked on 2026-11-04, offline:
  - the installed game is 1.7.104.0 (file version of `SkyrimSE.exe`), with SKSE 2.3.1 (`skse64_1_7_104.dll`);
  - the Address Library for it is installed: `Data/SKSE/Plugins/versionlib-1-7-104-0.bin`, format 5, header version 1.7.104.0;
  - the CommonLibSSE-NG pinned by `lib/skyrim-mod-toolchain` names 1.7.104 as its latest AE runtime (`RUNTIME_SSE_LATEST_AE` in `SKSE/Version.h`), and its `REL::IDDB` reads format-5 files from `versionlib-<version>.bin`.

  Checked in game on 2026-11-23 with the skyrim-mcp bridge (run in `qasmoke`): SKSE reports the plugin as loaded correctly on 1.7.104.0, and its log has the version line and `kPostLoad`, `kPostPostLoad`, `kInputLoaded`, `kDataLoaded`, then `kSaveGame`, `kPreLoadGame` and `kPostLoadGame` for a console save and load. `kNewGame` was not exercised, since `coc` from the main menu does not start a new game.
- **Sanitizer job (T1.3):**
  - CI time: the job repeats the toolchain setup action, which installs the whole cross toolchain (clang, Wine, llvm-mingw, xwin, sysroot) although the job needs only the native compiler and vcpkg. It runs after `checks`, so it adds about 3 to 4 minutes of wall time per run, and it is skipped when `checks` fails. A native-only mode of the setup action would cut this; the action belongs to `lib/skyrim-mod-toolchain`, so that change goes there, not into this plan.
  - The setup action's vcpkg cache key ends in the run ID, so two jobs of one run running in parallel race for the same key and the one that finishes first wins the save. With `needs: checks`, `checks` saves and `sanitizers` hits the key and skips its save (CI run 45917852333). Catch2 comes from that cache for the same `x64-linux` triplet and is not instrumented, which ASan and UBSan allow; errors inside Catch2 itself are not caught.
  - `catch_discover_tests` runs the instrumented test binary at build time; a sanitizer error during static initialization fails the build step instead of a test.
  - The sanitizers find only errors the tests execute. The PR #37 pattern shows only under a compiler without P2718R0 (GCC 13 in CI), so a local run with GCC 15 can pass where CI fails.
  - Settled (user decision, 2026-11-27, #50): the sanitizer run is one of the local checks, for the changes that [CONTRIBUTING.md#workflow](../../CONTRIBUTING.md#workflow) step 3 names.
- **Cross build:** CommonLibSSE-NG and the vcpkg ports must build with clang-cl and lld-link against xwin. Port build scripts that assume MSVC can fail. T1.1 showed that the CommonLibSSE-NG custom ports cover this (the directxtk shaders need Wine and llvm-mingw); for a new failing dependency the fallback is to vendor it as a submodule.
- **Papyrus under Wine (S8):** resolved: the compiler runs under Wine 10.0 with Wine Mono 11.3.0 ([S8 result](#s8-result)). What is left: CI cannot compile Papyrus without the Creation Kit's files, so the scripts are built and checked only locally, and a fresh Wine prefix needs Wine Mono installed by hand. The `.pex` header carries the build machine's user and host name, which R1 blanks.
- **S1 fails:** if the LevelUp menu cannot be suppressed cleanly, ST-19 and success criterion 1 need the revert fallback, or a PRD change. This is the highest-risk spike, so run it first. Resolved: suppression works with Experience ([S1 result](#s1-result)). What is left: 1.6.x, level-up menu mods and the game without Experience are untested (the hook has no 1.7.x-only offset). `setlevel`-style level changes are settled by ST-18: they are not level-ups, and G5 counts only the hook path.
- **Stat box text (S2, U1):** paging (C9c) counts the model's lines, not the lines the box draws, and a line longer than about 40 characters wraps. S2 showed that the box never cuts off or scrolls but shrinks its font, readable up to about 30 lines, so 15 lines per page leave room for wrapped lines; a page of many long provider lines can still become small. With vertical buttons, 10 fit with 16 lines of text; the ∞ glyph is missing, so C9d shows `--` ([S2 result](#s2-result)). Left open: the game without SkyUI, other resolutions and the gamepad's Y, which the user checks by hand in U1. If those differ, the ways out (a lower default, fewer buttons, short names only) are for the user to decide, since they change what ST-24 shows.
- **Warning write-back (C10, K1):** the plugin writes its own INI while the game runs. Under a mod manager's virtual file system (Mod Organizer 2) the write can land in another folder than the user's copy (the overwrite folder), or fail; a user editing the INI at the same time can lose an edit or the dismissal. K1 checks the write under the test install's setup and logs the path written; a failure only brings the warning back at the next load (assumption 18). If a message box does not show over the main menu, the warning moves to the first game start or load, as ST-28 allows (K1).
- **Escape in a message box (U1, K1):** if Escape or the gamepad's cancel button closes a box without calling the callback, or closes the skills menu behind it, the box flow of ST-21 and the warning may need `isCancellable` or `cancelButtonIndex` set; which behaviour is wanted is the user's decision (assumption 19).
- **Damage split (S4, G7):** the split sits at one call inside `DoDamage`, which nothing in the test install patches ([S4 result](#s4-result)). A combat overhaul that replaces `DoDamage` or that call makes the scan fail and turns the redirection off; one that only redirects `ProcessHit`'s call still reaches it. No overhaul was tested, and a lethal split hit, a hit split to no Health damage and poison were not seen in game, so G7 checks them.
- **END's small effects and the sneak reveal (S9, S10):** S10 may find no site for the breath time or the fall damage amount that works without an ESP, and S9 no cue that does; then ST-11's or ST-13a's effect cannot be built as the PRD has it, which goes back to the user. skyrim-acrobatics-ng in the test install may change fall damage itself, so G7a's effect has to be checked with it.
- **Check hooks (S6b):** resolved in part: persuasion and intimidation can be changed without an ESP by swapping their condition functions in the script function table ([S6b result](#s6b-result)). What is left: the conditions run before the dialogue menu opens and also outside persuasion, so G10c needs a filter that S6b did not find; if none works, the check would also change other Speechcraft conditions, which goes back to the user. Lockpicking, pickpocket and barter were not reached in game.
- **Magic crit (S5):** resolved: spell crits scale an effect's magnitude once, in `AdjustForPerks`, before any damage is dealt, so they do not overlap with the S4 split, which then splits the scaled damage ([S5 result](#s5-result)). What is left: G8 must tell cast spells and staves apart from hazards, enchantments and poisons (ST-13), and a combat overhaul that replaces 44016 or the vtable slots makes the scans fail; none was tested.
- **NPC actor-value persistence:** permanent modifiers on non-persistent NPCs reset when the actor respawns, but the ledger in the co-save does not. G2 and G3 must reconcile this, and OQ-1 may change how much NPC data exists at all.
- **Skill XP hook (S3, G6):** the hook sits inside the game's skill XP routine, at a call that Experience and CSF leave alone on 1.7.104. A plugin that patches that call, or a 1.6.x build with other bytes, makes the scan fail and turns the learning bonus off, with a log line only (*user decision, 2026-11-27:* no warning on screen) ([S3 result](#s3-result)). The same holds for the CSF site from S7.
- **CSF (S7):** resolved for CSF 3.2.0: CSF reports level-ups through its interface (G4), and G6 has a site after its perk entry call ([S7 result](#s7-result)). What is left: G4's `Advance` caller hooks and G6's hook depend on 3.2.0's code and `Skill` layout, so a CSF update turns CSF skill training and the CSF learning bonus off, with a log line only, until the plugin learns the new build. CSF skills above 100 and a real CSF use are untested.
