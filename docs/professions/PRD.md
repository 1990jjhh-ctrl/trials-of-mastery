# Trials of Mastery Professions: Product Requirements Document

Status: DRAFT, content moved from the original PRD; not yet through discovery
Date: 2026-11-01
Author: Joel Hirsbrunner

Part of Trials of Mastery; see the [overview](../PRD.md) for the module split. The v1 classes themselves are in [classes.md](classes.md).

## 1. Summary

The professions module is a framework for classes. A class gives a character starting stats, a category, boost points and XP modifiers, and decides how level-up stat points are distributed. Classes are data, so players and patches can add them. The module builds on the [stat module](../stats/PRD.md) and uses only its API.

## 2. Problem

NPCs have no profession-based strength: a farmer and a warrior are mechanically the same kind of actor, and the player has no class identity.

## 3. Goals and success criteria

Goals:

1. Every character, player and NPC, has a class that shapes how they train.
2. Classes are data that players and patches can add or change.

Success criteria: open, to be defined in this module's discovery.

## 4. Target users

- Players who want class identity on top of the stat module.
- Mod authors who add classes through patches.

## 5. Scope

In scope: class definitions, categories, boost points, XP modifiers, the class menu, level-up point distribution by class, and NPC class assignment.

Out of scope:

- The class content itself: [classes.md](classes.md).
- Realms, Ascension and the number of class slots over time: [mastery PRD](../mastery/PRD.md).
- New skill trees for trade and agriculture.

## 6. Terminology

| Term | Meaning |
| --- | --- |
| Class / Profession | Category, starting stats, main stats, boost points, XP modifiers, level stat boost profile and description |
| Category | Physical, Magic or Life. Every class belongs to one; a character holds at most one class per category |
| Physical skills | One-Handed, Two-Handed, Archery, Block, Heavy Armor, Light Armor |
| Magic skills | Destruction, Conjuration, Alteration, Illusion, Restoration |
| Life skills | Everything not meant for combat. Currently Smithing, Alchemy, Enchanting, Speech |
| Stealth skills | Sneak, Lockpicking, Pickpocket |

## 7. Requirements

Requirement IDs use the prefix PR and run in document order.

- PR-1: Each class defines a category, starting stats, main stats (specialization, gain bonus), boost points, XP modifiers per skill and per action, the stat boosts granted per level, a description, and a flag "player-selectable".
- PR-2: Every class belongs to exactly one category. A character holds at most one class per category.
- PR-3: Every class has two boost points: a primary boost point giving +20 % XP on one skill and a secondary boost point giving +10 % XP. The player chooses when a class offers several options; NPCs roll their picks at random.
- PR-4: The character's class modifies skill XP and the stat XP a skill gives per skill, and the stat XP an action gives per action, through the stat module's XP modifier hook (ST-17). All values are configurable. Individual stats are never penalized. The actions fighting, taking damage, overencumbered walking and sprinting have no class modifiers.
- PR-5: With several classes, the best (least negative) modifier per skill and per action applies, not an average, so hybrids such as a spellblade are not penalized. Example, Assassin + Wizard: only Smithing (-40 %), Heavy Armor, Block and Two-Handed (-60 % each) keep a modifier.
- PR-6: Balance rule: per class, boosts plus modifiers sum to the same total, −470 %. With +30 % from its boost points, a class carries −500 % in modifiers. Classes added later, including patch classes, follow the same rule.
- PR-7: Classes are defined in a JSON file that players and future mods can modify. Patches add classes with their own modifiers per skill and per action.
- PR-8: The player chooses a class in a custom menu after character creation.
- PR-9: Starting stats are assigned only for the first class.
- PR-10: The number of class slots is set through the module's API; the mastery module unlocks further slots. Secondary classes grant skill routing and modifiers.
- PR-11: The module takes over the stat module's level-up point assignment (ST-22) and distributes the points silently by the class profile.
- PR-12: All vanilla NPCs receive a class derived from their job.

## 8. Open questions

- Success criteria for this module.
- Number of class slots without the mastery module (one, in the draft).
- Whether hybrids need restrictions beyond the slot and category rules.
- Class modifiers: the values in [classes.md](classes.md) are initial values for playtesting; modifiers for further classes.
- Class catalog: all pools, fixed boosts, main stats and modifiers are drafts. In particular: the Berserker's dual-wield focus has no mechanic beyond One-Handed yet; the Battlemage's category (Magic in the draft; revisit its physical skills when implementing the class); Illusion and Restoration placement.
- Per-class flag "player-selectable": the Farmer is NPC-only in v1 and becomes selectable with its skill tree in a later version.
- Class descriptions for all 14 classes.
- Realm of Lorkhan: shrine-effect variant only, or class selection at shrines (needs one shrine per class or a selection menu).
