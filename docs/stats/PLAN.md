# Trials of Mastery Stats: Implementation Plan

Status: DRAFT, awaiting approval
Date: 2026-11-04
Implements: [Stats PRD](PRD.md) (ST-1 to ST-28). Shared rules: [overview](../PRD.md).

This plan does not restate requirements. Each task names the requirement IDs it covers. The PRD holds what they mean.

## Goal

Build the Stats module as an SKSE plugin for Skyrim AE. The formula and configuration core is a library with no game dependency, tested by unit tests outside the game ([success criterion 4](PRD.md#3-goals-and-success-criteria)). A separate game layer connects that core to the game, the stat box, the C++ and Papyrus API and compatibility detection.

## Conventions used in this plan

- **OQ-n** is the n-th bullet in [PRD §10 Open questions](PRD.md#10-open-questions): OQ-1 NPC values, OQ-2 default effect coefficients, OQ-3 skill XP split, OQ-4 learning-speed curve, OQ-5 check weights, OQ-6 END redirection share, OQ-7 direct activity XP amounts, OQ-8 warn or disable on an incompatible mod. A task that depends on an OQ ships a **placeholder default** in a config file and marks it `TODO(OQ-n)` there and in the log at load. The plan does not answer any OQ.
- **Verification types:** `unit` means a ctest test that runs on the dev host without the game. `log` means a check of the plugin log (`Documents/My Games/Skyrim Special Edition/SKSE/<plugin>.log`). `in-game` means a manual step list that the task adds to `modules/stats/tests/ingame/<task-id>.md`. In-game checks use the console command `cgf "<Script>.<Function>" ...` against the Papyrus API once A2 exists. Before A2 they use log dumps.
- **Parallel** means the tasks share no files except one-line additions to a CMake source list.
- Code style: the user's `write-code` skill applies to all C++ and Papyrus, unless the project states a deviation.

## Decisions taken

These are the user's answers to the decisions in the first draft of this plan.

| ID | Decision |
| --- | --- |
| D1 | CommonLibSSE-NG |
| D2 | No MSVC. The plugin is cross-built on Linux with clang-cl 21 and lld-link against the xwin splat (VC and Windows Kits) in `/home/joel-hirsbrunner/xwin-out`. CMake and ninja are installed. vcpkg is not installed yet and is set up by T1.1 |
| D3 | CMake |
| D4 | Catch2, nlohmann/json, SimpleIni |
| D5 | The Creation Kit Papyrus compiler under Wine (`<Skyrim>/Papyrus Compiler/PapyrusCompiler.exe`), with the vanilla sources and `TESV_Papyrus_Flags.flg` from `<Skyrim>/Data/Source/Scripts`. Caprica is not used |
| D6 | Assumptions confirmed or moved into the PRDs; see below |

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
3. **API registrations against JSON (confirmed, [overview §4](../PRD.md#4-shared-constraints)):** an entry registered through the API acts as that entry's default, and JSON patch files override it key by key.
4. **XP modifier combination (confirmed):** each registered XP modifier (ST-17) is a factor `1 + m` under its owner's key, and all factors multiply.
5. **Ascend through Papyrus (confirmed):** the ascend math lives in C++. The Papyrus binding only passes on a [curve](../PRD.md#41-curves) name and its parameters.
6. **Numeric types:** base, point counts and modifiers are signed 32-bit integers. XP, cost, multiplier and effective stat are `double`.
7. **Check-to-result mapping (OQ-5):** each check in the actions file gets an output mapping from `I` to the game quantity (sweet-spot size, pickpocket chance, price factor). The mapping is a curve with placeholder parameters.
8. **Registered stats whose owner is missing at load:** their saved data is kept but dormant. It is not shown and has no effects, and it comes back when the owner registers again. The PRD does not cover this case.
9. **Custom Skills Framework (CSF):** how the module learns about a CSF skill level-up is not known (spike S7). Until then ST-16 is covered for vanilla skills, skills above 100 and level-ups reported through the API.
10. **Game version (confirmed, [overview §4](../PRD.md#4-shared-constraints)):** 1.6.x and newer, tested on 1.7.104. T1.2 checks the Address Library and CommonLibSSE-NG support for 1.7.104.

## Tasks

### Phase 1: Setup

#### T1.1 Build and test skeleton

- **Goal:** CMake project with the core library target, the unit-test target and a placeholder plugin target that cross-builds with clang-cl. Dependencies come from vcpkg.
- **Covers:** success criterion 4 (infrastructure).
- **Scope:**
  - top-level `CMakeLists.txt` and `CMakePresets.json`;
  - `vcpkg.json` and vcpkg as a git submodule under `external/vcpkg`, with a bootstrap step in the README;
  - `cmake/toolchains/clang-cl-xwin.cmake`, with the xwin path as a cache variable;
  - a custom vcpkg triplet for the Windows cross build, using the toolchain file as chainload;
  - `modules/stats/CMakeLists.txt`, `.clang-format`, `.gitignore`;
  - `README.md` (setup, bootstrap, build and test commands only);
  - the "To fill in" section of `CLAUDE.md`, replaced by a link to the README section.
- **Interfaces:**
  - Targets: `tom_stats_core` (static library, standard C++ plus nlohmann/json and SimpleIni), `tom_stats_core_tests` (Catch2), `tom_stats_plugin` (Windows DLL, only in the cross preset).
  - Presets: `core` (native Linux, GCC or Clang) and `plugin` (clang-cl, lld-link, ninja).
- **Verification:**
  - `unit`: `cmake --preset core && cmake --build --preset core && ctest --preset core` passes with one trivial test, and `ctest --preset core -R <name>` runs a single test.
  - `cmake --preset plugin && cmake --build --preset plugin` produces an empty DLL that links against CommonLibSSE-NG.
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
  - the ST-15 example values come out (weapon fit, trade curve);
  - unknown curve names and missing parameters are rejected.
- **Depends on:** T1.1. **Parallel** with C2, C4.

#### C2 JSON merge engine

- **Goal:** load one table folder by the [overview merge rules](../PRD.md#4-shared-constraints) and return the merged object plus diagnostics.
- **Covers:** ST-27, [§9 Broken patches](PRD.md#9-risks).
- **Interfaces:**
  - `PluginQuery` (abstract: active plugins in load order, with name and extension).
  - `TableSource` (file name plus text). A filesystem enumerator is a thin helper on top.
  - `mergeTable(sources, PluginQuery&) -> MergeResult { json merged; vector<Diagnostic> }`. Diagnostics cover file order, conflicts, removed keys, warnings for same-name plugins and skipped invalid files.
- **Verification:** `unit`, one test per rule in the overview, including:
  - same-name plugins: the file loads at the last of them and a warning is logged;
  - overriding the default file is not reported as a conflict;
  - an invalid file is skipped and the remaining files still load.
- **Depends on:** T1.1. **Parallel** with C1, C4.

#### C3 Settings and tables

- **Goal:** typed configuration from the INI and the four merged tables, the shipped default files and the JSON schemas.
- **Covers:** ST-3, ST-9, ST-14 (mapping data), ST-15 (check and training data), ST-21 (groups), ST-27, ST-28 (incompatible list).
- **Scope:** `core/config/`, `modules/stats/data/trials-of-mastery-stats.ini`, `data/{stats,skills,actions,incompatible}/_default.json`, `data/schema/{stats,skills,actions,incompatible}.schema.json`.
- **Interfaces:**
  - `Settings`: the INI values of ST-27, plus the gamepad button of ST-24.
  - `StatsTable`: per stat, the group, base and effects (actor-value or named, each with a curve).
  - `SkillsTable`: per skill, the primary stat, secondary stat and split.
  - `ActionsTable`: per action, the check inputs with curves, the output mapping (assumption 7) and the training curves per stat.
  - `IncompatibleTable`: per entry, the game plugin names and SKSE plugin names to detect.
  - `loadSettings(text)` and `buildTables(MergeResult...) -> expected<Tables, Errors>`.
  - Names added by other mods are checked against the namespacing rule of ST-27.
- **Placeholders:**
  - Values: effect coefficients (OQ-2), the split ratio (OQ-3), learning-speed curves (OQ-4), check weights and output mappings (OQ-5), redirection share (OQ-6), direct activity XP (OQ-7).
  - The incompatible default lists Requiem plus a starter set of known RPG stat mods, marked "to be verified" (see [§7.9](PRD.md#79-compatibility)).
  - Each placeholder is marked `TODO(OQ-n)` in a `"_todo"` field beside its key, since JSON has no comments.
- **Verification:** `unit` tests check that:
  - the shipped defaults parse and validate against the schemas;
  - they contain the default mapping and groups of [ST-27](PRD.md#78-configuration);
  - invalid entries are reported with file and key;
  - INI defaults are loaded when keys are missing.
- **Depends on:** C1, C2. **Sequential.**

#### C4 Stat values, cost and progression

- **Goal:** per-stat and per-actor state with cost, XP gain with carry-over, advance, ascend and effective value.
- **Covers:** ST-1, ST-2 (registry), ST-4, ST-5, ST-6 (math), ST-7, ST-8.
- **Interfaces:**
  - `StatId`. `StatRegistry` holds the core stats plus registered ones, with a dormant flag (assumption 8).
  - `StatValue { int32 base, accumulated, active; double xp; }`.
  - `ActorStats`: stat values, start values, multiplier and `map<key,int32>` modifiers.
  - `double cost(double r, int32 k)` returns `+inf` on overflow.
  - `GainResult addXp(StatValue&, double xp, double r)`.
  - `advance(StatValue&)`.
  - `ascend(StatValue&, function<int32(int32)>)`.
  - `double effective(const ActorStats&, StatId)`.
- **Verification:** `unit` tests for the cost formula and overflow, several points from one large XP amount with carry-over, advance, ascend, effective value with several modifier keys and removal of one key, and no clamp at 100.
- **Depends on:** T1.1. **Parallel** with C1, C2.

#### C5 Effect math

- **Goal:** compute effect values and the per-actor-value deltas that the game layer applies.
- **Covers:** ST-10, ST-11, ST-12, ST-13 (math).
- **Interfaces:**
  - `avContribution(effect, effective, start)` per ST-10.
  - `ContributionLedger`: the last contribution per actor and actor value. `vector<AvDelta> updateContributions(ledger, actor, tables)`.
  - `RedirectSplit splitDamage(damage, share, poolAvailable) -> { fromPool, toHealth }`.
  - `double learningBonus(actor)`: the INT and PER curve terms, summed and then capped at the ST-12 hard cap. The cap applies to the stat bonus only. Outside bonuses are applied by G6, not here.
  - `double baseCritChance(actor, kind)`, with an additive shift slot for other modules (luck).
- **Verification:** `unit` tests check that:
  - a new character (effective = start) gets a zero contribution;
  - a stat change yields only the difference;
  - ascending shrinks the contributions;
  - the damage split is correct with an empty pool, a short pool and a full pool;
  - a curve set in the file to exceed the cap still yields at most the ST-12 cap.
- **Depends on:** C1, C3, C4. Placeholders depend on OQ-2, OQ-4, OQ-6.

#### C6 Training, checks and XP modifiers

- **Goal:** compute the stat XP from skill level-ups and actions, and check values.
- **Covers:** ST-14, ST-15 (math), ST-16 (levels above 100, registered mappings and actions), ST-17.
- **Interfaces:**
  - `XpModifierRegistry`, keyed by owner, per skill for skill XP, per skill for stat XP on level-up, and per action (assumption 4).
  - `skillLevelUpXp(skill, levelReached) -> vector<{StatId, double}>`.
  - `actionTrainingXp(action, input) -> vector<{StatId, double}>`.
  - `double checkValue(action, actor, skillLevels)`.
  - `double checkResult(action, I)` (assumption 7).
  - API entries are the defaults that JSON entries override, in one isolated merge step (assumption 3).
- **Verification:** `unit` tests check that:
  - the split applies to the level reached;
  - an unmapped skill gives no XP;
  - a skill at level 150 trains with the same rule;
  - modifiers from two owners combine;
  - checks sum the stat terms and the skill terms;
  - the precedence follows assumption 3.
- **Depends on:** C1, C3, C4. Placeholders depend on OQ-3, OQ-5, OQ-7.

#### C7 Stats service, level-up logic and events

- **Goal:** one facade over registry, actor states, tables, modifiers and events. The game layer and the API call only this facade.
- **Covers:** ST-6 (auto-advance, advance claim, N), ST-18, ST-20, ST-22 (claim flag), ST-26 (operations and events).
- **Interfaces:**
  - `StatsService` operations:
    - read values; `addXp`; `setValues`;
    - `setModifier` and `removeModifier`; `setMultiplier`;
    - `setR` (one value per save, player only, see ST-6 and [§5](PRD.md#5-scope)); `setN`;
    - `claimAdvancing(owner)`; `advance` and `advanceAll`; `ascend`;
    - `onPlayerLevelUp(newLevel)`;
    - `claimPointAssignment(owner)`; `assignFreePoint(stat)`.
  - `EventSink` interface with the five ST-26 events.
  - A stat change returns the `AvDelta`s from C5.
- **Verification:** `unit` tests check that:
  - free points from a level-up go to accumulated and do not change the cost;
  - several levels at once grant points per level;
  - auto-advance follows ST-6 and stops after a claim;
  - `setN` changes the advance interval;
  - events fire in the right order with the right payloads;
  - unspent points stay pending.
- **Depends on:** C4, C5, C6.

#### C8 Persistence format

- **Goal:** serialize and deserialize the full service state to versioned bytes, independent of the SKSE co-save.
- **Covers:** [§8 persistence constraint](PRD.md#8-constraints), success criterion 3 (format).
- **Interfaces:** `serialize(const StatsService&) -> bytes`, `deserialize(bytes, ActorIdResolver&) -> expected<State>`. `ActorIdResolver` maps saved actor IDs to current ones (FormID remap) or drops them. The format has a version field.
- **Verification:** `unit` tests check that:
  - all persisted fields of §8 round-trip, including dormant stats and N;
  - unresolved actors are dropped and logged;
  - an older format version is either read or rejected explicitly.
- **Depends on:** C7.

#### C9 Stat box model

- **Goal:** a pure state machine for the stat-box pages and their texts. It has no UI.
- **Covers:** ST-21, ST-22, ST-24 (content and flow).
- **Interfaces:** `StatBoxModel(service, maxButtons)`. `Page { text; vector<Button> }`. `press(buttonIndex) -> Page | Closed`.
- **Verification:** `unit` tests for every page transition in ST-21 and ST-22, for group pages when the stats plus "Back" exceed `maxButtons`, and for registered stats without a group landing in Other.
- **Depends on:** C7. **Parallel** with C8.

### Phase 3: Spikes (uncertain game hooks)

Each spike is a throwaway prototype in the plugin with a time box. Its result is a short update of the affected task in this plan (hook point, Address Library ID, fallback), which the user approves. The spikes depend on T1.2 and can run in parallel with each other and with Phase 2.

| ID | Question | Feeds |
| --- | --- | --- |
| S1 | Can the vanilla LevelUp menu be suppressed while the level and the perk point are still applied right away and the +10 is never granted (ST-19)? Where is the moment the menu would open? Does it behave the same with Experience and with a level-up menu mod? Fallback if suppression fails: let the vanilla flow run and revert the +10 (must be invisible to the player). | G5 |
| S2 | Can a native message box open over the skills menu with SkyUI and without it, and can a key or gamepad button be caught only while that menu is open (ST-24)? How many buttons fit in one box (ST-21 grouping)? Is a hint possible without changing the skills menu (ST-25)? | U1, U2 |
| S3 | Where can skill XP be multiplied so that it stacks multiplicatively with Guardian Stones, Fortify effects and other mods (ST-12, ST-17), instead of replacing them or being overwritten? | G6 |
| S4 | Where can incoming damage be split before it reaches Health, separately for physical and magic damage (ST-11)? Does that hook coexist with combat overhauls? | G7 |
| S5 | How can DEX set the base crit chance for physical attacks and for spells, given that vanilla has no spell crits (ST-13)? | G8 |
| S6a | Hook points for every default training action of ST-15. | G9a to G9c |
| S6b | Hook points for the default checks of ST-15. Persuasion and intimidation are dialogue conditions in vanilla. | G10a to G10c |
| S7 | How does the module learn about Custom Skills Framework skill level-ups (ST-16)? Do skill level-up events still fire above 100 with Beyond Mastery and Experience? | G4 |
| S8 | Papyrus build with the Creation Kit compiler under Wine (D5). This starts from the current error "Wine Mono is not installed": installing Wine Mono or .NET in the Wine prefix is a **user action**. Then compile a stub script with a native function from a CMake custom command, with the Skyrim path as a cache variable, and bind it from the plugin. | A2 |

### Phase 4: Game layer (`modules/stats/plugin/`)

#### G1 Configuration loading in game

- **Goal:** load the INI and the four tables at `kDataLoaded`, with a `PluginQuery` backed by the game's loaded-plugin list.
- **Covers:** ST-3, ST-27, ST-28 (the table is loaded here; K1 acts on it).
- **Interfaces:** uses C2, C3, C7. A game `PluginQuery` adapter.
- **Verification:** `log`. The log lists file order, conflicts, same-name warnings, skipped files and each `TODO(OQ-n)` placeholder in use. A test patch and a plugin-named file for an inactive plugin behave as C2 specifies.
- **Depends on:** T1.2, C3, C7.

#### G2 Co-save persistence and actor lifecycle

- **Goal:** store and restore the service state through the SKSE serialization interface, including revert on new game or load.
- **Covers:** ST-4, [§8](PRD.md#8-constraints), success criterion 3.
- **Interfaces:** SKSE serialization callbacks (save, load, revert) with a unique ID and record types. The `ActorIdResolver` uses the SKSE FormID resolution. NPC defaults are the stats-file base until the API sets values (**placeholder, depends on OQ-1**).
- **Verification:**
  - `in-game`: values survive save, quit and load; a new game resets them; going from a save without the plugin and back does not crash.
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
- **Interfaces:** a skill-increase event sink feeding `StatsService`. The CSF path follows the S7 result.
- **Verification:** `in-game` plus `log`. `advskill` on One-Handed logs the split; a skill level above 100 trains; an unmapped skill gives nothing.
- **Depends on:** G1, S7. The split depends on OQ-3.

#### G5 Level-up handling

- **Goal:** react to every player level-up, grant free points, auto-advance, suppress the vanilla reward and open the stat box at the moment found by S1.
- **Covers:** ST-6 (auto-advance in game), ST-18, ST-19, ST-20, ST-23, success criterion 1.
- **Interfaces:** a level-increase sink and the S1 hook. It calls `onPlayerLevelUp` and asks U1 to open the box.
- **Verification:** `in-game` for:
  - success criterion 1, with vanilla leveling and with Experience;
  - the perk point arriving right away;
  - advancing at multiples of N;
  - no vanilla menu, with and without a level-up menu mod.
- **Depends on:** S1, G2. U1 is needed for the box, but G5 can be checked by log first.

#### G6 Learning speed and skill XP modifiers

- **Goal:** multiply skill XP by the capped learning bonus from C5, the other skill XP bonuses and the per-skill modifiers, as ST-12 and ST-17 define.
- **Covers:** ST-12, ST-17 (skill XP).
- **Verification:** `in-game` plus `log`:
  - the logged skill XP gain changes by the expected factor;
  - with a Guardian Stone active, the gain is the product of both bonuses;
  - with the stats at the curve limit, the stat factor is ×3 at most, and outside bonuses still apply on top.
- **Depends on:** S3, G1. The curve depends on OQ-4.

#### G7 END damage redirection

- **Goal:** redirect the END share of incoming damage into Stamina or Magicka per the C5 split.
- **Covers:** ST-11.
- **Verification:** `in-game` plus `log`. With full, partial and empty pools, the logged split matches C5.
- **Depends on:** S4, G3. The share depends on OQ-6.

#### G8 DEX crit chance

- **Goal:** apply the DEX base crit chance to physical and magic attacks, with the additive shift slot for other modules.
- **Covers:** ST-13.
- **Verification:** `in-game` plus `log`. The logged crit chance follows the curve, and over many hits the crit rate is close to it.
- **Depends on:** S5, G1. The curve depends on OQ-2.

#### G9 Action training hooks

These are three tasks with no shared files, all after S6a and G1, each **parallel** with the others. A fourth task, G9d, is not a hook.

- **G9a Combat:** swing, bow draw, crossbow load, hit, staves, unarmed and gloves, fighting, spell cast.
- **G9b Body:** damage taken, disease recovery, poison resistance, sprinting with stamina use, overencumbered walking.
- **G9c Social:** trade by money value, persuasion and intimidation by target, normal interactions.
- **G9d Mod actions:** the shared `applyTraining` entry for API-registered actions (ST-16). It needs no hook. A1 wires it up.
- **Covers:** ST-15 (training), ST-16, ST-17 (stat XP per action).
- **Verification:** `in-game` plus `log`. Each action logs its XP, and the ST-15 example values (dagger, Stendarr's Hammer, 100 septims) come out.
- **Depends on:** XP amounts depend on OQ-7.

#### G10 Action checks

Three tasks, all after S6b and C6, each **parallel**:

- **G10a:** lockpicking and pickpocket.
- **G10b:** sneak and detection.
- **G10c:** persuasion, intimidation and barter.
- **Covers:** ST-15 (checks).
- **Verification:**
  - `log`: shows `I` and the resulting game value per attempt.
  - `in-game`: raising DEX widens the sweet spot; a novice skill level with a high stat still fails master-level checks.
- **Depends on:** weights and mappings depend on OQ-5.

### Phase 5: Stat box UI

#### U1 Stat box

- **Goal:** show the C9 pages as native message boxes. They open by key in the skills menu and from G5.
- **Covers:** ST-21, ST-22, ST-24.
- **Interfaces:** an input sink that is active only while the skills menu is open and uses the INI key and gamepad button. A message-box adapter that renders a `Page` and returns the pressed index.
- **Verification:** `in-game`:
  - the key opens the box with and without SkyUI;
  - every ST-21 flow works;
  - unspent points survive Cancel and save/load;
  - group pages appear with a registered test stat;
  - with a point-assignment claim, only "OK" shows.
- **Depends on:** S2, C9, G5.

#### U2 Skills-menu hint (Could)

- **Goal:** show the key hint in the skills menu, but only if S2 shows this works without changing that menu.
- **Covers:** ST-25.
- **Verification:** `in-game`. The hint is visible, and the skills menu is unchanged with SkyUI and with CSF.
- **Depends on:** S2, U1. Drop this task if S2 is negative.

### Phase 6: API

#### A1 C++ API

- **Goal:** a versioned public C++ API for other SKSE plugins.
- **Covers:** ST-2, ST-6 (claim advancing, set N), ST-8, ST-16, ST-17, ST-22, ST-26.
- **Scope:** `modules/stats/include/tom/stats/api.h` (header only, no CommonLib types beyond forward-declared actors) and the plugin-side implementation.
- **Interfaces:**
  - An interface struct with a version number, handed out through the SKSE messaging interface on request.
  - Its functions map one to one onto `StatsService`, and event callbacks are registered per event type.
  - Registrations are accepted until `kDataLoaded` and merged per assumption 3.
  - Calls are allowed only on the main thread, and the header documents this.
- **Verification:** `unit` for the version check and argument validation in the core part. A3 does the end-to-end check.
- **Depends on:** C7, G1.

#### A2 Papyrus API

- **Goal:** a compiled Papyrus script with native functions and Papyrus events.
- **Covers:** ST-26, assumption 5.
- **Scope:** `modules/stats/papyrus/` (the script source, with the `.pex` as a build output from the S8 custom command) and the native bindings in the plugin.
- **Interfaces:**
  - Global native functions that mirror A1, with stats addressed by name.
  - Ascend takes a curve name and parameters and passes them on to the C++ ascend.
  - Events are delivered as SKSE mod events with documented names and arguments.
- **Verification:** `in-game`. `cgf` calls read and change values, a test script receives each event, and `Papyrus.0.log` shows no stack dumps.
- **Depends on:** S8, A1.

#### A3 API consumer test plugin

- **Goal:** a small separate SKSE plugin that runs success criterion 2.
- **Covers:** success criterion 2.
- **Scope:** `modules/stats/tests/consumer/` (a separate DLL target, not shipped).
- **Verification:** `log`. Each step of success criterion 2 is logged with pass or fail.
- **Depends on:** A1, G4, G9d.

### Phase 7: Compatibility and release checks

#### K1 Incompatible-mod detection

- **Goal:** check the entries of the incompatible table at load against the loaded game plugins and SKSE plugins, then log, warn and disable, or run with the INI override.
- **Covers:** ST-28.
- **Interfaces:** uses `IncompatibleTable` (C3, loaded by G1). Adds a module "disabled" state: hooks stay inert, and the API reports that the module is disabled.
- **Verification:**
  - `unit`: entries match against a fake plugin list.
  - `in-game` plus `log`: with a dummy `Requiem.esp`, the log shows the detection, a warning appears, and the module stays inert, or runs with the override set; a patch file adding an entry is honoured.
- **Depends on:** G1, A1 (the disabled state must reach the API). The behaviour depends on OQ-8.

#### V1 Release test pass

- **Goal:** run success criteria 1 to 3 in one documented session: vanilla leveling, Experience, save and load, and a long play session.
- **Covers:** [§3 success criteria](PRD.md#3-goals-and-success-criteria) 1 to 3.
- **Verification:** `in-game` checklist in `tests/ingame/V1.md`, with no crashes and with clean `Papyrus.0.log` and plugin logs.
- **Depends on:** all tasks above.

### Order and parallelism

- Sequential path: T1.1, then C4 and C1 and C2, then C3, C5, C6, C7, C8, G1, G2, G3, G5, U1, A1, A2, A3, K1, V1.
- Parallel tracks:
  - T1.2 and the spikes S1 to S8 run alongside Phase 2. S8 waits on the Wine Mono user action.
  - C1, C2 and C4 run in parallel, and so do C8 and C9.
  - G6, G7, G8, G9a to G9c and G10a to G10c run in parallel once their spike and G1 or G3 are done.

## Risks

- **Game version (T1.2):** testing covers only 1.7.104. Address Library IDs found in the spikes must exist for 1.6.x as well, or the claim "1.6.x and newer" is untested there.
- **Cross build:** CommonLibSSE-NG and the vcpkg ports must build with clang-cl and lld-link against xwin. Port build scripts that assume MSVC can fail. T1.1 shows this early; the fallback is to vendor the failing dependency as a submodule.
- **Papyrus under Wine (S8):** this is blocked until Wine Mono or .NET is installed in the prefix. If the compiler still fails, A2 is blocked; fallback: revisit D5 (Caprica).
- **S1 fails:** if the LevelUp menu cannot be suppressed cleanly, ST-19 and success criterion 1 need the revert fallback, or a PRD change. This is the highest-risk spike, so run it first.
- **Check hooks (S6b):** persuasion and intimidation in vanilla are dialogue conditions on Speech, not a formula. Replacing them may need condition-function hooks, or may not be possible without an ESP, which the "no ESP" constraint rules out.
- **Magic crit (S5):** vanilla has no spell crits. Adding them may need a damage hook that overlaps with S4 and with combat overhauls.
- **NPC actor-value persistence:** permanent modifiers on non-persistent NPCs reset when the actor respawns, but the ledger in the co-save does not. G2 and G3 must reconcile this, and OQ-1 may change how much NPC data exists at all.
- **CSF (S7):** if CSF exposes no level-up signal, ST-16 for CSF skills needs CSF users or a patch to report level-ups through the API.
