# Trials of Mastery: Mastery Product Requirements Document

Status: DRAFT, content moved from the original PRD; not yet through discovery
Date: 2026-11-01
Author: Joel Hirsbrunner

Part of Trials of Mastery; see the [overview](../PRD.md) for the module split. Builds on the [stat module](../stats/PRD.md) and the [professions module](../professions/PRD.md) through their APIs.

## 1. Summary

The mastery module adds realms (Novice to Master), class-specific trials that gate each major breakthrough, Ascensions after Master, and a coherent strength for every actor in the world.

## 2. Problem

Vanilla Skyrim has no meaningful progression gates, and level scaling lets a level-5 character kill the first dragon. Guards are weak. Progression ends at skill 100.

## 3. Goals

1. Realms (Novice, Apprentice, Adept, Expert, Master) for the player and all actors.
2. Major realm breakthroughs gated by class-specific trials.
3. Coherent world strength: guards are at least Expert, dragons are above Master, the main quest is endgame content.
4. Unbounded progression after Master through resets ("Ascensions", counter N) that give a minor, bounded benefit.

## 4. Target users

- Skyrim AE players who want harder, more coherent progression.
- Players of Survival Mode (overnight minor level-ups) and of Beyond Mastery.
- Mod authors who want to add trials through patches (new spells, creatures).

## 5. Terminology

| Term | Meaning |
| --- | --- |
| Realm | Novice, Apprentice, Adept, Expert, Master. A major breakthrough moves to the next realm |
| Level | Minor level inside a realm; silent, automatic stat boost |
| Ascension (N) | Reset after Master; N counts completed resets. Working name; dragons use a separate label |
| level_r | Stat points gained since the current realm entry (resets to 0 at every major breakthrough) |
| Exam / Consequence / Ascension | Three-phase gate for a major breakthrough |

## 6. Requirements

Requirement IDs use the prefix MA and run in document order.

### 6.1 Stat scaling

These requirements use the stat module's API (ST-6, ST-8).

- MA-1: The growth factor of the stat cost formula is `r_N = 1 + 1 / (1 + √N)`, and its exponent is `level_r`.
- MA-2: `level_r` resets to 0 only at major breakthroughs, not at minor level-ups.
- MA-3: The stat multiplier is `m^realm_index` with a tunable realm multiplier `m` (initial value ≈ 1.3), so that a Master with N=0 beats a lower-realm actor of any N. Pools scale with `m` through the stats.

### 6.2 Levels and realms

- MA-4: Minor level-ups are silent and automatic. They add stat points according to the class profile, and the pools follow from the stats. In Survival Mode they occur overnight.
- MA-5: Level progression derives from class skills only. Skills outside the class do not contribute to the level.
- MA-6: Realm thresholds are configurable per class and need not be equally spaced (example: Novice 1–6, Apprentice 7–12, Adept 13–18, Expert 19–24, Master 25+).
- MA-7: Vanilla leveling and level scaling are fully replaced.

### 6.3 Class slots

- MA-8: After the first Ascension, the player can choose a second class, and further classes at higher N. Slots = `min(3, 1 + N)` (tunable), set through the professions module's API (PR-10).
- MA-9: For later classes, the converted stats of the previous cycle are used (carry-over `⌊√gain⌋`, applied through the stat module's ascend, ST-6), and stats below the new class's starting values are raised to them (to be confirmed).

### 6.4 Major breakthrough

- MA-10: A breakthrough happens in three phases: Exam, Consequence, Ascension. The realm rises only after the Consequence is resolved.
- MA-11: The player initiates the Exam when the level threshold is reached. The Exam is a measurable counter or event (kill, profit, spell use, quest stage).
- MA-12: The Consequence tests the class's own stats with class-appropriate stakes:
  - Combat/stealth: lethal opposition.
  - Magic: backlash or rival caster.
  - Craft/trade: loss of assets or reputation, not life.
  - Social: reputation or intrigue.
- MA-13: Consequence pressure scales with the realm of the breakthrough.
- MA-14: A hidden, randomised time limit applies to Consequences; at expiry, the remaining pressure converges for a final encounter (the quest ends only when resolved).
- MA-15: Sleep and wait are allowed; encounters trigger on wake after a randomised interval, with a telegraphed warning.
- MA-16: Fast travel and equivalent teleports are blocked during hunt-based Consequences.
- MA-17: Non-lethal failure triggers a setback: the saved snapshot of stats and `level_r` from the last minor level-up is restored (pools follow from the stats; at most one minor level, never below realm entry). The Exam re-triggers after a cooldown once the threshold is reached again. No XP refund.
- MA-18: Death means reload from the last save.

### 6.5 v1 trials

Exams and consequences are concepts for the Expert to Master step. The rows for Berserker, Assassin, Wizard, Merchant and Farmer were discussed; the other nine rows are first drafts. The classes themselves are in [classes.md](../professions/classes.md).

| Class | Exam | Consequence |
| --- | --- | --- |
| Berserker | Clear a named dungeon or defeat a named enemy | Rival or revenge fight |
| Guardian | Hold a named position or escort through a night-long attack | Retaliation by a stronger rival force |
| Archer | Kill a named beast at long range with a set number of shots | Rival hunters track the player |
| Assassin | Spawned target, found, killed and disposed of unseen; not discovered for a few days | "Survive": 10 hunters at or above the player's realm in staggered waves, with stealth and detection skills |
| Thief | Steal a named item from a guarded location unseen | The owner sends hunters; capture counts as failure |
| Cleric | Cure a set number of afflicted NPCs of a plague | Outbreak; failure = patients lost above a threshold |
| Battlemage | Defeat a named enemy using both weapon and spell in the same fight | Rival battlemage |
| Wizard | Master a spell (use it N times under set conditions); modded spells optional | Spell backlash or rival caster |
| Necromancer | Create a new summon (patch), or raise a set number of corpses (fallback) | Witch-hunters track the player |
| Merchant | 10,000 septims profit | Rival, tax collector or robbery targeting stock; failure = value below threshold |
| Enchanter | Craft an enchantment above a value threshold | The enchantment destabilizes; it must be stabilized |
| Alchemist | Brew a new recipe above a value threshold | A scandal or plague demands a cure; failure = reputation below threshold |
| Blacksmith | Forge a masterwork above a quality threshold | A customer or rival disputes the quality, proven by a stress test or duel |
| Farmer (NPC only) | Grow a new crop | Blight, pests or drought; failure = yield below threshold |

Early-realm exams may be simple (e.g. join the Dark Brotherhood, complete its questline). Mod-dependent trials live in optional patch plugins with a vanilla fallback.

### 6.6 Actors

- MA-19: All vanilla NPCs receive a realm and N on top of their class (PR-12). Stat values are derived with the same formulas from a fixed XP budget per realm (no per-NPC simulation).
- MA-20: N is rolled once at spawn with `P(N ≥ k) = p^k` (p ≈ 0.1 civilians, ≈ 0.25 guards and veterans). Named characters use hand-set values. An actor with k classes (for example a spellblade, a mage plus a fighter class) has N ≥ k - 1.
- MA-21: Guards are at least Expert (mixed warrior and wizard classes), N mostly 0–1. Irileth N=1–2 and the guard captain N=1 can defeat the first dragon together with guards. The Blades are very strong.
- MA-22: Dragons are at least one level above Master (e.g. Master level 7 if realms have 6 levels), N≈3 for the first dragons. Paarthurnax N ≥ 10, below Alduin in overall strength, who is the strongest. Dragons use a renamed reset axis.
- MA-23: The first dragon must not be soloable by a Novice. The player needs a way to land the killing blow for soul absorption (design pending).
- MA-24: Animals, daedra and undead receive class-like profiles with realm and N, tuned per creature type.

### 6.7 Compatibility

Status levels and domain ownership are defined in the [overview](../PRD.md#5-compatibility). Stat-mod conflicts are handled by the [stat module](../stats/PRD.md#79-compatibility). Mod names and behaviour come from general knowledge, not from a check against Nexus or the current versions; each entry must be tested before compatibility is promised.

| Mod or category | Status | Conflict | Resolution and owner |
| --- | --- | --- | --- |
| Requiem | Incompatible | Replaces leveling and perks | Detected at load; warn and disable the class system (MA-27) |
| Experience | Integrated | Also replaces the player XP and level curve and defines skill caps | Experience owns player XP and skill caps; skills contribute no XP. ToM owns the level/realm system and takes XP and level events from Experience. Interface not yet verified |
| Beyond Mastery | Integrated | Raises skill caps above 100 | Owns caps when Experience is absent. With Experience loaded, caps are defined by Experience |
| Realm of Lorkhan | Integrated | Shrines and their effects | Class shrines get different effects; the debuffs stay unchanged. Alternative: class selection at the shrines. Decision open |
| Skill-uncapper mods | Incompatible or redundant | Change the same cap values as Beyond Mastery and Experience | Do not load alongside. Beyond Mastery or Experience owns caps |
| Perk overhauls (Ordinator, Vokrii, Adamant, Simonrim) | Configurable | Perks are keyed to skill levels and perk points; ToM replaces leveling and therefore perk-point grants | The perk mod owns perks; ToM owns perk-point grants (amount per level or realm is an open decision) |
| Level-scaling and leveled-list mods | Configurable | Alter actor level and spawn lists, where ToM sets class, realm and N | Spawn mod owns spawn lists. ToM owns actor stats and level derivation and applies its profile after spawn; mods that rewrite actor level scaling are redundant |
| NPC and creature overhauls (e.g. Immersive Creatures, bandit mods) | Configurable | Change Actor Values, classes and AI packages | ToM applies the profile after the mod's changes unless the actor is on the exception list. Spawning and behaviour stay with the mod |
| Follower mods (e.g. Inigo, Auri) | Configurable | Their own level and skill scripts | Exempt by default (exception list). Optional patch assigns a class and realm |
| Dragon mods (new dragons, dragon combat) | Configurable or Patch | Compete over dragon stats and the soul-absorb logic | ToM owns dragon realm and N by race or keyword rule. The mod keeps behaviour and visuals. Needs a patch for new dragon races |
| Main-quest level checks (Alduin) | Patch | Hard-coded level conditions fail without vanilla leveling | ToM replaces or bypasses them through quest condition patches |
| Survival mods (Campfire, Survival Mode Improved) | Compatible | Overnight level-up and survival timing | ToM detects survival state for overnight minor level-ups; test |
| Alternate-start mods (e.g. Live Another Life) | Compatible | Run before character creation and give start items and perks | The class menu opens afterwards; test start items and perks |
| Spell and magic mods | Patch | Needed for exams such as creating a new summon | Optional patch plugins, with a vanilla fallback |
| SPID and KID | Compatible | None; useful for distribution | ToM may use them for rule-based class, realm and N distribution, or ship its own pipeline |

- MA-25: At load, ToM detects the mods in the table through the SKSE plugin list and plugin presence, and logs the result.
- MA-26: Each domain in the [ownership table](../PRD.md#52-domain-ownership) has exactly one runtime writer. An INI flag per domain (XP, caps, perk points) chooses the owner.
- MA-27: When an incompatible mod is detected, ToM shows a warning at load and disables its class system, unless an INI override allows an unsupported run.
- MA-28: With Experience loaded, ToM takes XP and level events from Experience and treats skill XP as zero. Skill caps follow Experience.
- MA-29: Actors on an exception list (follower mods, quest actors) keep their own stats. The list is editable by INI and by patch plugins. Mod-specific patches ship as separate optional plugins; the base plugin runs without them.

## 7. Constraints

- Fast-travel blocking must be re-asserted periodically and on cell change, and must cover carriages, ships, Recall/Mark, mod teleports and dragon rides.
- Hunter spawns must start out of line of sight; no `MoveTo` next to the player.
- Hunters must be in a faction that guards treat as neutral.
- Timers use game-time registration so they advance during sleep and wait.
- Persisted state: exam phase, snapshots, N, class slots and hidden timers must survive save/load.
- Actor load: limit simultaneous hunters (2–3 per wave).

## 8. Success criteria

1. A Master N=0 actor beats a Novice N=5 actor in controlled tests at equal equipment.
2. The first dragon cannot be killed solo by a level-equivalent Novice player; guards plus Irileth defeat it.
3. All 13 player-selectable v1 classes can be played from Novice to Master (the Farmer is NPC-only in v1), with exams, consequences and ascension working across save/load.
4. No exploit path skips a Consequence (fast travel, sleeping through it, leaving the area).
5. No hard crashes or Papyrus stack dumps over a 20-hour playthrough with 100+ spawned NPC profiles.
6. Standalone and Beyond Mastery configurations both pass the same test plan.
7. With Experience installed, XP, skill caps and stats each have exactly one writer, and the same test plan passes.
8. With an incompatible mod installed, the mod detects it at load and follows the defined behaviour (MA-27) instead of running partially.

## 9. Risks

- **Scope:** all actor types (NPCs, animals, daedra, undead, dragons) in v1 alongside 14 classes is large. Mitigation: ship the actor pipeline generically and tune creature types incrementally.
- **Balance:** realm multiplier `m`, N rolling probability and consequence pressure need playtesting; all are exposed as config values.
- **Compatibility:** mods that change leveling, perks, or actor stats may conflict with the replaced vanilla level system. The mod list comes from general knowledge and is not verified against Nexus or the mods' current versions.
- **Difficulty parity:** lethal combat Consequences versus soft craft Consequences may produce uneven difficulty; pressure scales with realm to compensate.
- **Brute-force bypass:** strong characters might kill their way through social or economic Consequences; thresholds are counter-based, not kill-based.
- **Hidden timer:** randomised hidden limits may feel unfair; telegraph encounters.

## 10. Open questions

- Final name of the reset axis for players versus dragons.
- Value of `m`, `p`, cooldown length, and the hidden timer range.
- How the player lands the killing blow on a dragon far above their stats.
- Exam and consequence content for each class beyond v1, and for realm steps below Expert.
- Class change at an Ascension: the exact conversion of the previous cycle's stats into starting values (carry-over `⌊√gain⌋` agreed earlier, which classes it applies to), and whether stats below the new class's starting values are raised.
- Exams and consequences of the nine undiscussed classes are first drafts (6.5).
- Experience: API or event interface for XP and skill caps (not yet checked); how level-ups map to ToM's minor levels.
- Perk points: how many are granted per minor level or realm when a perk overhaul is present.
- Behaviour on detecting an incompatible mod: warn only, or disable the class system (MA-27 currently disables).
