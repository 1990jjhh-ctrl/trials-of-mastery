# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

Trials of Mastery is a Skyrim AE mod package made of separate modules (stats, professions, mastery, luck). The build and test setup exists; module code does not yet.

Check the `Status:` line of a module's PRD before implementing it. While it reads DRAFT, work on the PRD, not on code.

## Where things are defined

- Module split, dependencies, build order, shared constraints and domain ownership: [docs/PRD.md](docs/PRD.md)
- Per-module requirements: [docs/stats/PRD.md](docs/stats/PRD.md), [docs/professions/PRD.md](docs/professions/PRD.md), [docs/mastery/PRD.md](docs/mastery/PRD.md), [docs/luck/PRD.md](docs/luck/PRD.md)
- The v1 classes: [docs/professions/classes.md](docs/professions/classes.md)
- Each PRD ends with its open questions. Don't settle one in code; raise it with the user.

Modules talk to each other only through the public API of the module they depend on ([docs/PRD.md#2-modules](docs/PRD.md#2-modules)).

## Build and test

Setup, build and test commands, including running a single test: [README.md#build-and-test](README.md#build-and-test). Code style: [.clang-format](.clang-format).

## Workflow

Branches, commits, pull requests and what CI checks: [CONTRIBUTING.md](CONTRIBUTING.md). Before reporting a change as done, run the local checks in [CONTRIBUTING.md#workflow](CONTRIBUTING.md#workflow) (step 3) and report their results, including any you could not run.

Work on a module or task goes through the agents in this order:

1. `planner` breaks an approved PRD into a plan of tasks (e.g. [docs/stats/PLAN.md](docs/stats/PLAN.md)).
2. `implementer` implements one task of the plan on its own branch.
3. `reviewer` and `verifier` check the finished implementation, in parallel, before its pull request is merged.

## To fill in once code exists

How the SKSE plugins, Papyrus scripts and ESPs work together.
