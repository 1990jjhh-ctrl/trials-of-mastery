# Trials of Mastery Luck: Product Requirements Document

Status: DRAFT, content moved from the original PRD; not yet through discovery
Date: 2026-11-01
Author: Joel Hirsbrunner

Part of Trials of Mastery; see the [overview](../PRD.md) for the module split.

## 1. Summary

The luck module adds Luck (LUC) as a stat, registered through the [stat module](../stats/PRD.md)'s API. LUC comes from the player's favour with the divines and shifts the outcome of checks in both directions.

## 2. Goals and success criteria

Success criterion: at the vanilla maximum of a check (I = L), luck changes the outcome by nothing; over many trials, the luck function shifts results by no more than 25 % of the distance to L.

## 3. Scope

In scope: the LUC stat, divines and favour, offerings, the luck function, and NPC LUC.

Out of scope: the core stats and their effects ([stat module](../stats/PRD.md)).

## 4. Requirements

Requirement IDs use the prefix LU and run in document order.

- LU-1: The module registers LUC as a stat through the stat module's API. LUC affects drop rates, crit and chances in activities.
- LU-2: LUC is evaluated per action. Its value y lies in [-100, +100]: positive means luck, negative means bad luck. The player's alignment x runs from evil to good.
- LU-3: Every divine (Aedra and Daedra) has a fixed alignment value and a set of assigned activities. Activities cover crafts and trades (alchemy, hunting, lockpicking, sneaking), social acts (trade, intimidation, speech), and combat and magic. Every action belongs to an activity and has an alignment.
- LU-4: A favour table stores the player's standing with each divine, bounded to -100 to +100 (an unbounded raw value maps to this range, -inf to -100 and +inf to +100). For an action, y is the favour with the divine assigned to its activity.
- LU-5: The player makes offerings to Aedra and Daedra as they see fit. An offering raises favour with its recipient (scaled by item value) and lowers favour with every other divine, scaled by the alignment distance between them and the recipient.
- LU-6: Luck acts in both directions. With positive y the outcome of a check improves; with negative y it worsens. Example: the player kills a deer while favour with Hircine is -30, and the loot yield is lower.
- LU-7: The luck function turns the check value `I` of an action (from stats and skills, ST-15) into `μ = I + (y / 400) · (L - I)`, with `L` the vanilla maximum for that check. `μ` is clamped to [0, L] and replaces `I`. It applies to success chances, crit chance (on top of the DEX base, ST-13) and yield or drop rates.
- LU-8: When `I = L`, LUC has no effect. With high enough stats (for example DEX for sneak) the vanilla maximum is reachable, so mastered checks are immune to luck.
- LU-9: By construction LUC shifts a check by at most 25 % of the distance to `L`, so it decides close calls and never dominates. The divisor (400) is a config value.
- LU-10: NPCs also have LUC. It is derived from class, realm, N and an alignment roll; no favour table is simulated. Named characters use hand-set values.

## 5. Constraints

The favour table is stored per save and survives save/load.

## 6. Open questions

- Role of x (alignment) in practice, how y is derived when several divines own an activity, the assignment of activities and alignments to the divines, the definition of L per check type, and the offering formulas (favour gain by item value, penalty scaling by alignment distance, saturation function).
- Whether actions themselves change favour, or only offerings do.
- NPC LUC (LU-10) uses class, realm and N: how it works when the professions or mastery module is missing.
