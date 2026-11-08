# Contributing

How changes get into Trials of Mastery. Setup, build and test commands are in [README.md](README.md); what to build is in the PRDs ([docs/PRD.md](docs/PRD.md)).

## Workflow

Every change goes through a branch and a pull request; nothing is committed to `main` directly.

1. **Branch** from `main`, named `<module>/<task-id>-<slug>`, e.g. `stats/t1.1-build-setup`.
2. **Commit** one logical change per commit, so the pull request can be reviewed commit by commit. Fix a commit that is not merged yet by folding the fix into it (`git commit --fixup=<commit>`, then `git rebase --autosquash main`) instead of adding a fix-up commit.
3. **Check** locally before opening the pull request: `scripts/test.sh`, `lib/skyrim-mod-toolchain/scripts/format.sh --check modules` and `lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules` ([README.md#build-and-test](README.md#build-and-test)). A change to a plugin also needs `scripts/build.sh` and the in-game checks its task names.
4. **Open a pull request** against `main`. It is merged with a merge commit, so the branch's commits land on `main` as they are.

## Commit messages

Titles follow [Conventional Commits](https://www.conventionalcommits.org): `<type>(<scope>): <description>`, with the module as the scope where a change belongs to one, e.g. `feat(stats): ...`. The types and line lengths are in [.gitlint](.gitlint).

[gitlint](https://jorisroovers.com/gitlint/) checks every commit message. Install it (`pipx install gitlint`, or your distribution's package) and run `gitlint install-hook` once per clone, so a bad message is rejected when you commit. `gitlint --commits main..HEAD` checks a branch's commits.

## CI

[.github/workflows/ci.yml](.github/workflows/ci.yml) runs on every pull request and every push to `main`, and must pass before a merge. It runs the same scripts as step 3 of the workflow, with the same tool versions:

- gitlint on the pull request's commits;
- `scripts/test.sh`, the native unit tests of the `core` preset;
- `lib/skyrim-mod-toolchain/scripts/format.sh --check modules`;
- `lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules`;
- `scripts/build.sh`, the cross build of the plugin DLLs (`plugin` preset) and the runtime-import check.

The toolchain is set up by the action in `lib/skyrim-mod-toolchain` ([README.md#setup](README.md#setup)). Nothing in the game is covered, so run the in-game checks a task names yourself.

## Editor settings

[.editorconfig](.editorconfig) sets indentation, line endings and final newlines for every file type in the repository, so editors without a clang-format integration write files the way the formatter and the existing files expect. Most editors read it natively; VS Code needs the EditorConfig extension.

In VS Code, [.vscode/tasks.json](.vscode/tasks.json) runs the scripts of step 3 as tasks (`format: check`, `format: fix`, `test`, `lint`) and turns their findings into links in the Problems panel.
