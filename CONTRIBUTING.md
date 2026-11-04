# Contributing

How changes get into Trials of Mastery. Setup, build and test commands are in [README.md](README.md); what to build is in the PRDs ([docs/PRD.md](docs/PRD.md)).

## Workflow

Every change goes through a branch and a pull request; nothing is committed to `main` directly.

1. **Branch** from `main`, named `<module>/<task-id>-<slug>`, e.g. `stats/t1.1-build-setup`.
2. **Commit** one logical change per commit, so the pull request can be reviewed commit by commit. Fix a commit that is not merged yet by folding the fix into it (`git commit --fixup=<commit>`, then `git rebase --autosquash main`) instead of adding a fix-up commit.
3. **Check** locally before opening the pull request: `scripts/test.sh`, `scripts/format.sh --check` and `scripts/lint.sh` ([README.md#build-and-test](README.md#build-and-test)). A change to a plugin also needs `scripts/build.sh` and the in-game checks its task names.
4. **Open a pull request** against `main`. It is merged with a merge commit, so the branch's commits land on `main` as they are.

## Editor settings

[.editorconfig](.editorconfig) sets indentation, line endings and final newlines for every file type in the repository, so editors without a clang-format integration write files the way the formatter and the existing files expect. Most editors read it natively; VS Code needs the EditorConfig extension.
