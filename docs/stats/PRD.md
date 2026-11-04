# Trials of Mastery Stats: Product Requirements Document

Status: APPROVED
Date: 2026-11-02
Author: Joel Hirsbrunner

Part of Trials of Mastery; see the [overview](../PRD.md) for the module split.

## 1. Summary

The stat module replaces the vanilla Health/Magicka/Stamina level-up choice with eight trained stats. Stats gain XP from skill level-ups and from actions, drive the pools and other effects defined as data, and receive free points at every level-up. The module ships as its own SKSE plugin with INI and JSON configuration and no ESP, inside the Trials of Mastery package. It runs on its own and is the base that the other modules build on through its API.

## 2. Problem

Vanilla Skyrim gives a character no attributes: a level-up adds 10 points to one pool, and how the character fights, casts or sneaks has no effect on what they become. The later modules (professions, mastery, luck) need a stat base to build on, and that base has to stay independent of how and when a level-up happens, so it works with vanilla leveling, with Experience, and with the mastery module.

## 3. Goals and success criteria

Goals:

1. Characters have eight stats that grow through what they do.
2. Level-ups give stat points instead of pool points, whatever triggered the level-up.
3. Other modules and mods extend the stat system through an API without changing it.

The module is done when:

1. **Experience parity:** level-ups from vanilla leveling and from Experience both grant free stat points, and the +10 Health/Magicka/Stamina reward is never granted.
2. **API consumer test:** a small test plugin registers a stat, a skill mapping and an action, adds XP, reads a check value and receives the "point gained" event.
3. **Save/load stability:** stats, XP and registered stats survive save/load, with no crashes or Papyrus stack dumps over a long play session.
4. **Formula checks:** XP costs, carry-over, advancing, ascending, effective stats, effects, check values and the merging of configuration files match the formulas in this document in unit tests that run outside the game.

## 4. Target users

- Skyrim AE players who want attribute-based progression, with or without the rest of Trials of Mastery.
- The other Trials of Mastery modules, which use only the API.
- Mod authors who add stats or actions, connect custom skills (Custom Skills Framework) or read stats in their own mods.

## 5. Scope

In scope (v1):

- The eight core stats, their XP and cost formula, effects and pools.
- Stat training from skill level-ups and from actions.
- Actions: checks that combine stats and skills, and the stat XP each action gives.
- Free stat points at level-up, assigned by the player in the stat box.
- The stat box: an info box opened from the skills menu that also replaces the vanilla level-up menu.
- The API (C++ and Papyrus).
- Detection of incompatible stat mods.

Out of scope:

- Deciding when a level-up happens: vanilla, Experience or the mastery module decide.
- Perk points: unchanged; to be revisited in the mastery module.
- Luck: the luck module registers LUC as an additional stat ([luck PRD](../luck/PRD.md)).
- Classes, class XP modifiers and automatic point distribution: [professions PRD](../professions/PRD.md).
- Realms, the realm multiplier value and when stats advance once the mastery module is installed: [mastery PRD](../mastery/PRD.md).
- Followers that train their stats alongside the player: a later version.
- A dedicated stats menu: a later optional module that reads the API.
- Changes to the vanilla Stats (skills) menu: it is Scaleform and conflicts with Custom Skills Framework and other skill-menu mods.

## 6. Terminology

| Term | Meaning |
| --- | --- |
| Core stats | Strength (STR), Dexterity (DEX), Constitution (CON), Intelligence (INT), Wisdom (WIS), Perception (PER), Endurance (END), Charisma (CHA) |
| Registered stat | A stat added by another module through the API, for example Luck (LUC) |
| Pools | Health, Stamina, Magicka |
| Base | A stat's starting value |
| Active points | Points gained through XP since the stat last advanced; they set the cost of the next point |
| Accumulated points | Points outside the cost calculation: free level-up points and active points moved there by advancing |
| Modifier | An additive value from gear, effects or other mods; it is not scaled by the multiplier |
| Effective stat | `(base + accumulated + active) · multiplier + modifier`; all effects use it |
| Advance | Moving a stat's active points into its accumulated points, which resets the cost to 100 |
| Ascend | Converting a stat's accumulated and active points into base with a formula from the caller |
| Start value | A stat's base at character creation; the reference for pool contributions |
| `k` | The number of active points; the exponent of the cost formula |
| `r` | Growth factor of the cost formula |
| Stat box | The message box that shows the stats and handles level-ups (ST-21, ST-24) |
| Effect | What a stat changes in the game: an actor-value effect (ST-10) or a named effect (ST-11 to ST-13) |
| Action | Something the character does that the module hooks: it can have a check and give stat XP (ST-15) |
| Check value | `I`, the value an action's check computes from stats and skills |

## 7. Requirements

Requirement IDs use the prefix ST and run in document order.

### 7.1 Stats

- ST-1: The module provides the eight core stats.
- ST-2: Other modules can register additional stats through the API. A registered stat gets XP, the cost formula, effects, display and the level-up prompt like a core stat. Its effects come from its entry in the stats file (ST-9) or are implemented by the module that registers it.
- ST-3: The player starts with a base value per stat, set in the stats file, until another module (professions) sets the base.
- ST-4: Stats exist for every actor. Only the player trains stats; other modules set NPC values through the API (no per-NPC training).

### 7.2 XP and cost

- ST-5: XP accumulates per stat. Each stat point costs `XP = 100 · r^k`, with `k` the stat's active points. On gaining a point, the cost is subtracted, the remainder carries over and the point counts as active.
- ST-6: `r` is set in the INI and through the API. Advancing a stat moves its active points into its accumulated points, so `k` is 0 and the next point costs 100 again. Advancing also discards the stat's leftover XP, which would otherwise buy points right away at the lower cost. Without the mastery module, the module advances all stats whenever the new player level is a multiple of N (N in the INI). Another module can claim advancing through the API; the module then does not advance on its own, and the claiming module advances stats and may set N. The mastery module claims it. Ascending a stat applies a conversion formula to its accumulated plus active points, adds the result to the base, and sets accumulated points, active points and XP to 0. The module does not ascend on its own; the caller passes the formula through the API (the mastery module uses its carry-over formula, MA-9). The formula returns whole points.
- ST-7: There is no hard stat cap. Costs that overflow the float range are treated as infinite. Base, point counts and modifier are integers; the multiplier and the effective stat are floats. Derived formulas contain no implicit clamp at 100.
- ST-8: Effective stat = `(base + accumulated + active) · multiplier + modifier`. The multiplier defaults to 1 and is set per actor through the API; the mastery module sets it to its realm multiplier. Other modules and mods add to and remove from the modifier through the API, each under its own key, so they do not overwrite each other.

### 7.3 Effects

Default effects:

| Stat | Actor-value effects | Named effects |
| --- | --- | --- |
| STR | Physical damage, carry capacity | |
| DEX | | Crit chance, physical and magic (ST-13) |
| CON | Health pool and recovery; Magicka and Stamina recovery; resistance to poison and disease | |
| INT | Magicka pool; resistance to magic damage | Learning speed (ST-12) |
| WIS | Magicka recovery | Magic damage |
| PER | | Learning speed (ST-12) |
| END | Stamina pool and recovery; minor Health recovery | Damage redirection (ST-11) |
| CHA | | |

What stats do in skill activities (sneak, pickpocket, lockpicking, detection, speech, intimidation) is defined by actions (ST-15).

- ST-9: Effects are data. Each stat's entry in the stats file lists its effects, each with a [curve](../PRD.md#41-curves). An effect is either an actor-value effect (ST-10), which works for any actor value, or a named effect that the module implements and the file parameterizes (ST-11 to ST-13). Stats never raise skill levels. Once the stats file exists, it holds the effects and the table above only describes them.
- ST-10: An actor-value effect adds `curve(x) − curve(0)` to an actor value, with `x = effective stat − start value`, so a new character has the vanilla values. Pools are actor-value effects: by default Health from CON, Stamina from END, Magicka from INT. Ascending shrinks the pools together with the stats, in line with the level reset that comes with it. Pools never receive points directly. The module never sets an actor value's absolute value: it stores its last contribution per actor value in the save and, when a stat changes, adds only the difference to the actor value's permanent modifier. Race values, Fortify effects and other mods' changes stay as they are.
- ST-11: END redirects a small share of damage taken into a pool: physical damage into Stamina, magic damage into Magicka. When the pool is empty the damage goes directly to Health. When the pool holds less than the redirected amount, the available amount is taken from the pool and the remainder goes to Health. The share is a function of END, with its parameters in the stats file.
- ST-12: Learning speed comes from INT and PER, each up to +100 % skill XP, added together for a maximum of +200 % (×3) skill XP for every skill, with the curve in the stats file. The +200 % is a hard cap on the stat bonus, whatever curve the file sets; bonuses from outside the module are not capped. It multiplies with other skill XP bonuses (Guardian Stones, effects, other mods) and never replaces them. XP modifiers from other modules (ST-17) apply on top.
- ST-13: DEX sets the base crit chance for physical and magic attacks, with the curve in the stats file. The luck module may shift it through the API.

### 7.4 Training and actions

- ST-14: Skills train stats through a skill-to-stat mapping in the skills file. Each time a skill levels up, the stat XP equals the level reached and is split between the skill's primary and secondary stat at a configurable ratio. A default for the vanilla skills ships with the module, and patches and the API add or replace entries.
- ST-15: The module ships a fixed set of actions, each with its own game hook. For each action, the actions file defines:
  - a check: a [curve](../PRD.md#41-curves) per stat and per skill, with the effective stat or the skill level as input, giving `I = Σ curve(x)` over all inputs. The module turns `I` into the game result of that action, such as the lockpicking sweet spot, the pickpocket chance or the barter price. The skill level keeps its own weight, so a high stat does not make a novice succeed at master-level checks;
  - training: which stats the action trains and a curve for the XP of each.

  Default training:
  - Using a weapon trains STR per swing: `XP = α · W^λ` (power curve) with `α ≈ 0.665` and `λ ≈ 0.589` (a fit to an iron dagger, W = 2, giving 1 XP and Stendarr's Hammer, W = 100, giving 10 XP). W is the weapon's weight. For bows and crossbows, `W = weight + damage`, and STR XP is given on drawing (crossbows: on loading).
  - Every hit trains DEX with a fixed amount, for all attacks including spells and ranged weapons.
  - Staves train INT instead of STR, with the same weight formula. A use is each activation of the staff's ability (it consumes charge).
  - Unarmed attacks train DEX per hit as above and STR depending on the weight of the gloves (0 STR XP without gloves).
  - Fighting itself trains PER.
  - Taking damage, recovering from diseases and resisting poisons train CON and END.
  - Casting spells trains INT and WIS.
  - Sprinting (it must consume Stamina) trains END. Walking while overencumbered trains STR; a character is overencumbered when the carried weight is above its maximum carry capacity.
  - Trading trains CHA: `XP = 0.1 · M^0.5` (power curve, `α = 0.1`, `λ = 0.5`), with M the money value of the transaction: 100 septims give 1 XP, 1000 septims about 3 XP, 10 000 septims 10 XP. Persuading and intimidating train CHA, with the amount depending on the target; intimidating does not train STR. Normal interactions give 1 XP.

  Default checks: lockpicking (Lockpicking, DEX), pickpocket (Pickpocket, DEX), sneak (Sneak, DEX), detection of sneaking targets (PER), persuasion and barter (Speech, CHA), intimidation (Speech, CHA, STR). Once the actions file exists, it holds the values and this list only describes them.
- ST-16: Skills from other mods, including Custom Skills Framework skills, train stats only when a mapping is registered for them. Skill levels above 100 (Beyond Mastery, Experience) keep training stats with the same rule. Other mods can add actions: a mod registers the action through the API, hooks the game event itself, and asks the module for the check value and to apply the action's training. Patches can then tune the action like any other.
- ST-17: Other modules can register XP modifiers through the API: per skill for skill XP and for the stat XP a skill gives on level-up (ST-14), and per action for the stat XP the action gives (ST-15). The professions module uses this for its class modifiers.

### 7.5 Level-up

- ST-18: The module does not decide when a level-up happens. It reacts to every player level-up, whether vanilla, Experience or another mod triggered it.
- ST-19: The stat box replaces the vanilla level-up menu, and the +10 Health/Magicka/Stamina reward is never granted. The stat box opens at the moment the vanilla level-up menu would open. The level-up itself (level and perk point) is applied right away; only spending the free points can wait.
- ST-20: Each level-up grants free stat points; the number is set in the INI (default 3). Free points go to the accumulated points and do not raise the cost of trained points.
- ST-21: While free points are unspent, the stat box shows "Level Up" and "Cancel" instead of "OK". "Cancel" closes the box and keeps the points for later; they are stored in the save. "Level Up" shows the number of free points and one button per stat with its current value, plus "Back", and repeats until all points are spent or the player goes back. Registered stats appear like core stats. When the stats and "Back" do not fit in one box, the box first shows one button per stat group, and each group shows its stats. Each stat belongs to a group, set in the stats file.
- ST-22: Another module can take over point assignment through the API. The stat box then shows only "OK", and that module distributes the points (the professions module does this by class profile).
- ST-23: Perk points stay as in vanilla.

### 7.6 Display

- ST-24: While the skills menu is open, a key opens the stat box. It lists each stat's effective value and its XP toward the next point, and closes with "OK" (or offers "Level Up" and "Cancel", ST-21). The key and a gamepad button are set in the INI, and the mod description names the default key. This works with and without SkyUI.
- ST-25 (Could): While the skills menu is open, a hint shows the key, if this works without changing the skills menu (§5). A later optional module can replace the display through the API.

### 7.7 API

- ST-26: The module exposes a versioned API for SKSE plugins (C++) and for Papyrus. It covers:
  - reading base, accumulated points, active points, modifier and effective value of any stat for any actor;
  - adding XP to a stat;
  - setting stat values for an actor (NPC setup);
  - adding and removing modifiers under a key;
  - events: XP gained, point gained, level-up points granted, stat advanced, stat ascended;
  - registering stats, skill-to-stat mappings, actions and XP modifiers;
  - reading an action's check value for an actor and applying an action's training;
  - setting the multiplier, `r` and N, claiming and performing advancing, and ascending stats with a formula;
  - taking over level-up point assignment.

### 7.8 Configuration

- ST-27: The INI holds only single settings: `r`, N (ST-6, default 5), free points per level-up, the stat box key and the override of ST-28. Tables live in four JSON files, which players and other mods patch by the merge rules in the [overview](../PRD.md#4-shared-constraints):
  - stats: group, base value and effects per stat;
  - skills: the skill-to-stat mapping and the primary to secondary split;
  - actions: checks and training per action;
  - incompatible: the mods ST-28 detects, by plugin or SKSE plugin name.

  Default skill-to-stat mapping (primary, secondary):

| Skill | Stats | Skill | Stats |
| --- | --- | --- | --- |
| One-Handed | STR, DEX | Alchemy | INT, WIS |
| Two-Handed | STR, END | Enchanting | INT, WIS |
| Block | END, STR | Destruction | WIS, INT |
| Heavy Armor | END, CON | Conjuration | WIS, INT |
| Light Armor | DEX, END | Alteration | INT, WIS |
| Archery | DEX, PER | Illusion | WIS, PER |
| Sneak | DEX, PER | Restoration | WIS, CON |
| Lockpicking | DEX, PER | Speech | CHA, WIS |
| Pickpocket | DEX, PER | Smithing | STR, INT |

Default stat groups, named after the guardian stones: Warrior (STR, CON, END), Mage (INT, WIS), Thief (DEX, PER), Other (CHA and registered stats that name no group, such as LUC).

Once the JSON files exist, they hold the values and this section only describes them. Names of stats, skills and actions added by other mods are namespaced (`<Mod>:<Name>`).

### 7.9 Compatibility

Status levels and domain ownership are defined in the [overview](../PRD.md#5-compatibility). Mod names and behaviour come from general knowledge and must be tested before compatibility is promised.

| Mod or category | Status | Conflict | Resolution |
| --- | --- | --- | --- |
| Requiem | Incompatible | Replaces attributes | Detected at load (ST-28) |
| RPG stat and attribute mods | Incompatible | Write the same stats with different definitions and effects | Detected at load (ST-28) |
| Experience | Compatible | Changes how and when level-ups happen | Level-ups from Experience grant stat points (ST-18) |
| Beyond Mastery | Integrated | Raises skill caps above 100 | Skill levels above 100 keep training stats (ST-16) |
| Custom Skills Framework | Integrated | Adds skills | Its skills train stats once a mapping is registered (ST-16) |
| Race overhauls (Races of Skyrim) | Compatible | Change race base pools | Pool contributions are added to the pools, never set (ST-10) |
| Combat overhauls (Valhalla Combat, Wildcat) | Compatible | Damage, stamina and blocking formulas | The combat mod owns formulas; test balance |
| Level-up menu mods | Compatible | Change or replace the vanilla level-up menu | The stat box replaces the vanilla level-up menu (ST-19); test which one opens |
| UI mods (SkyUI) | Compatible | None | The stat box is a native message box (ST-24) |

- ST-28: At load, the module detects the mods in the incompatible file (by default Requiem and known RPG stat mods) through the SKSE plugin list and plugin presence, logs the result, shows a warning and disables itself, unless an INI override allows an unsupported run.

## 8. Constraints

- SKSE C++ plugin for stats, XP, effects, the stat box and the API; a compiled Papyrus script for the Papyrus API. No ESP.
- Stats, start values, XP, accumulated and active points, modifiers, `r`, multipliers, pool contributions, unspent free points and registered stats are stored per save in the SKSE co-save and survive save/load.

## 9. Risks

- **Discoverability:** the stat box opens only by key in the skills menu or on a level-up. Accepted: Trials of Mastery players get the box on every level-up, and the key is in the mod description.
- **Level-up interception:** replacing the vanilla level-up menu must work with Experience and with mods that change that menu; otherwise level-ups lose their points or show both menus.
- **Balance:** coefficients, check weights, the learning-speed ceiling (×3), the cost growth and N need playtesting; all are config values.
- **Broken patches:** players edit the JSON files by hand. An invalid file is skipped and logged, and the remaining files still load.
- **Grind:** stat XP can be farmed (swings, hits, damage, casts, weights). This is accepted by design, as training in the real world is repetition. The mitigations are small XP amounts, exponentially rising costs and the class modifiers, with no rate limits.

## 10. Open questions

- What values do NPCs have when only the stat module is installed?
- Coefficients and curves of the default effects, including pools and recovery.
- Primary to secondary split of skill level-up XP: 3:1 or 2:1. The default mapping is a draft.
- Learning speed: curve from INT and PER to the +100 % ceiling each.
- Check weights and curves per action (DEX and Lockpicking, CHA and Speech, ...): to be found by testing approaches during implementation.
- END redirection: share of damage as a function of END.
- Direct activity XP (ST-15): the amounts per DEX hit, spell, damage taken, disease and poison (they must stay small); the XP for persuading and intimidating by target. Direct XP and skill level-up XP stack.
- Behaviour on detecting an incompatible mod: warn only, or disable (ST-28 currently disables).
