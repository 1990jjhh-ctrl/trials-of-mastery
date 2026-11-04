# Trials of Mastery: Overview

Status: DRAFT, awaiting approval
Date: 2026-11-02
Author: Joel Hirsbrunner

## 1. Summary

Trials of Mastery replaces vanilla Skyrim leveling with trained stats, professions, and realm progression gated by class trials. It ships as one package of separate modules, each with its own PRD. This document holds only what the modules share.

## 2. Modules

| Module | Purpose | Ships as | Depends on | PRD |
| --- | --- | --- | --- | --- |
| Stats | Trained stats replace the vanilla level-up reward | DLL, INI, JSON | none | [stats/PRD.md](stats/PRD.md) |
| Professions | Classes for the player and all actors | to be decided | Stats | [professions/PRD.md](professions/PRD.md), [classes.md](professions/classes.md) |
| Mastery | Realms, class trials, Ascensions, world strength | to be decided | Stats, Professions | [mastery/PRD.md](mastery/PRD.md) |
| Luck | LUC from divine favour | to be decided | Stats | [luck/PRD.md](luck/PRD.md) |

- A module uses other modules only through their public API.
- A module runs without the modules that depend on it.
- Build order: Stats, Professions with the class descriptions, Mastery. Luck any time after Stats.

## 3. Non-goals

- Multiplayer or non-AE game versions (SE 1.5.97 and VR).
- Replacing vanilla skill trees, perks or magic systems beyond what the stat formulas require.

## 4. Shared constraints

- Skyrim AE 1.6.x and newer. Testing is on 1.7.104.
- Each module is an SKSE C++ plugin plus Papyrus/ESP where it needs quests, menus or forms.
- Tables that players and other mods tune (mappings, classes, modifiers) are JSON files; scalar settings are INI values.
- JSON files are patched by merging, the same way in every module:
  - Each table has a folder. The module's default file, `_default.json`, loads first.
  - Files without a plugin name load next, in alphabetical order. A file named after a plugin without its extension (`Requiem.json` for `Requiem.esp`) loads only while that plugin is active, in plugin load order, after the unnamed files. When several active plugins share that name (`Foo.esm` and `Foo.esp`), the file loads at the last of them and a warning is logged.
  - Objects merge key by key, recursively for nested objects: a key in a later file replaces the same key, a new key is added, a key the file leaves out stays. `null` removes a key.
  - Entries that a mod registers through a module's API count as defaults: JSON patches override them key by key.
  - Entries are objects keyed by name, never lists. Names added by other mods are namespaced (`<Mod>:<Name>`).
  - When two patch files set the same key, the later one wins and the conflict is logged. Overriding the default file is not a conflict.

### 4.1 Curves

Every formula that a JSON file configures uses one of these curves. `x` is the input (a stat, a skill level, a weight, a money value). A file names the curve and gives its parameters.

| Curve | Formula | Parameters |
| --- | --- | --- |
| linear | `a · x + b` | `a`, `b` |
| power | `α · sign(x) · \|x\|^λ` | `α`, `λ`; `λ = 0.5` is the square root. Mirrored for `x < 0`, so a negative input gives a negative value |
| asymptotic | `E + (S − E) · e^(−\|c\| · x)` | `S`, the value at `x = 0`; `E`, the end value approached as `x` grows; `c`, how fast `E` is approached |

## 5. Compatibility

The rule is single ownership: for each domain exactly one mod or module writes at runtime, and the others read. Each module PRD lists the mods that concern it.

### 5.1 Status levels

| Status | Meaning |
| --- | --- |
| Incompatible | Replaces the same systems. Not supported; no patch planned |
| Integrated | The module knows the mod and hands specific domains to it, or takes them over, by design |
| Configurable | Works with an INI flag or exception list that decides who owns a domain |
| Compatible | No shared writes expected; test only |
| Patch | Needs an optional separate plugin |

### 5.2 Domain ownership

| Domain | Owner | With Experience |
| --- | --- | --- |
| Player XP and level-up trigger | Vanilla; Mastery when installed | Experience |
| Level-up reward (stat points instead of Health/Magicka/Stamina) | Stats; Professions distributes the points when installed | Stats |
| Core stats, stat XP, stat effects, pools | Stats | Stats |
| Stat advance (cost reset to 100) | Stats every N levels; Mastery when installed | Stats |
| LUC | Luck | Luck |
| Skill-level caps | Beyond Mastery, if installed | Experience |
| Class, category, class XP modifiers | Professions | Professions |
| Realm, N, exams, actor strength | Mastery | Mastery |
| Perks | Installed perk mod | Installed perk mod |
| Perk-point grants | Vanilla; Mastery when installed (open) | Experience or Mastery (open) |
| Spawn lists | Installed spawn/creature mod | Installed spawn/creature mod |
