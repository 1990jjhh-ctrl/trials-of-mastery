# Trials of Mastery: v1 Classes

Status: DRAFT

The class content for the [professions framework](PRD.md). The framework's rules (categories, boost points, cheapest modifier, balance rule) are in the PRD. Once the classes JSON file exists, it holds the values and these tables only describe them.

## Catalog

| Class | Category | Main stats | Primary Skill (20 %) | Secondary Skill (10 %) |
| --- | --- | --- | --- | --- |
| Berserker | Physical | STR, END | One-Handed, Two-Handed | Heavy Armor, Light Armor |
| Guardian | Physical | END, STR | One-Handed (+10 %) + Block (+10 %) | Light Armor, Heavy Armor, Restoration |
| Archer | Physical | PER, DEX | Archery | Light Armor |
| Assassin | Physical | DEX, PER | Alchemy, Archery, One-Handed | Sneak, Light Armor, Illusion |
| Thief | Life | DEX, CHA | Lockpicking, Pickpocket | Sneak, Speech |
| Cleric | Life | WIS, CON | Restoration | Alteration, Enchanting, Alchemy, Block, Speech |
| Battlemage | Magic | INT, END | Destruction | Conjuration, Alteration, Enchanting, One-Handed, Heavy Armor |
| Wizard | Magic | INT, WIS | Destruction, Conjuration, Alteration, Illusion, Restoration | Enchanting, Alchemy |
| Necromancer | Magic | INT, CON | Conjuration | Destruction, Illusion, Alteration, Enchanting, Alchemy |
| Merchant | Life | CHA, INT | Speech | Alchemy, Smithing, Enchanting |
| Enchanter | Life | INT, PER | Enchanting | Alteration, Destruction, Smithing, Speech |
| Alchemist | Life | INT, PER | Alchemy | Alteration, Restoration, Speech |
| Blacksmith | Life | STR, END | Smithing | Heavy Armor, Light Armor, One-Handed, Two-Handed, Enchanting |
| Farmer (NPC only in v1) | Life | END, STR | none (Agriculture) | Alchemy, Smithing |

Hybrids are combinations of classes from different categories, for example a Paladin = Guardian + Cleric, or a spellblade = Assassin + Wizard or Berserker + Battlemage, depending on play style.

The Farmer is NPC-only in v1. Its player-facing version needs an agriculture skill tree, which is a later version of the mod. With only its +10 % secondary boost in v1, it carries −480 % in modifiers to meet the balance rule.

## XP modifiers

Initial values; a blank cell means no modifier.

| Class | Illusion | Conjuration | Destruction | Restoration | Alteration | Enchanting | Smithing | Heavy Armor | Block | Two-Handed | One-Handed | Archery | Light Armor | Sneak | Lockpicking | Pickpocket | Speech | Alchemy |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Berserker | -60 % | -60 % | -60 % | -60 % | -60 % | -25 % | | | -60 % | | | -40 % | | -25 % | -25 % | -25 % | | |
| Guardian | -60 % | -60 % | -60 % | | -60 % | -25 % | | | | -60 % | | -60 % | | -25 % | -25 % | -40 % | | -25 % |
| Archer | -60 % | -60 % | -60 % | -60 % | | -40 % | | -60 % | -60 % | -60 % | -40 % | | | | | | | |
| Assassin | | -60 % | -60 % | -60 % | -60 % | -40 % | -40 % | -60 % | -60 % | -60 % | | | | | | | | |
| Thief | -25 % | -60 % | -60 % | -60 % | -40 % | -25 % | -25 % | -60 % | -60 % | -60 % | | | | | | | | -25 % |
| Cleric | -40 % | -60 % | -60 % | | | | -25 % | -25 % | | -25 % | | -60 % | -25 % | -60 % | -60 % | -60 % | | |
| Battlemage | -40 % | | | -25 % | | | -60 % | | -60 % | -60 % | | -60 % | -60 % | -60 % | -25 % | -25 % | | -25 % |
| Wizard | | | | | | | -60 % | -60 % | -60 % | -60 % | -40 % | -60 % | -40 % | -40 % | -40 % | -40 % | | |
| Necromancer | | | | -25 % | | | -60 % | -60 % | -60 % | -60 % | -40 % | -60 % | -60 % | -25 % | -25 % | -25 % | | |
| Merchant | -40 % | -40 % | -40 % | -40 % | -40 % | | | -40 % | -40 % | -40 % | -40 % | -40 % | -40 % | | | -60 % | | |
| Enchanter | -25 % | -25 % | | -25 % | | | | -60 % | -60 % | -60 % | -25 % | -60 % | -60 % | -25 % | -25 % | -25 % | | -25 % |
| Alchemist | -25 % | -25 % | -25 % | | | | -60 % | -60 % | -60 % | -60 % | -25 % | -25 % | -60 % | -25 % | -25 % | -25 % | | |
| Blacksmith | -60 % | -60 % | -60 % | -60 % | -60 % | | | | -25 % | | | -40 % | | -60 % | -25 % | -25 % | | -25 % |
| Farmer | -40 % | -40 % | -40 % | -40 % | -40 % | -60 % | | -40 % | -25 % | | | -25 % | -25 % | -25 % | -40 % | -40 % | | |

## Descriptions

Pending for all 14 classes.
