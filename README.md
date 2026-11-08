# Trials of Mastery

A Skyrim AE mod package of SKSE plugins (stats, professions, mastery, luck). Goals, scope and the module split are in [docs/PRD.md](docs/PRD.md).

## Setup

On a Linux host, run once `lib/skyrim-mod-toolchain/scripts/setup.sh` (after `git submodule update --init --recursive`). What it installs and the environment variables it prints for your shell profile are described in [lib/skyrim-mod-toolchain/README.md#setup](lib/skyrim-mod-toolchain/README.md#setup).

## Build and test

Everything, in one go: the native unit tests, then the cross build of the plugin DLLs and a check that they do not need the player's C++ runtime:

```sh
scripts/build.sh
```

The DLLs end up in `build/plugin/modules/<module>/`. The first run builds the vcpkg dependencies and takes a while.

Native unit tests only:

```sh
scripts/test.sh
scripts/test.sh -R "<test name>"   # single test
```

`scripts/test.sh` runs the `core` preset (`cmake --preset core`, `cmake --build --preset core`, `ctest --preset core`); the cross build uses the `plugin` preset. Both are in [CMakePresets.json](CMakePresets.json).

Formatting:

```sh
lib/skyrim-mod-toolchain/scripts/format.sh modules           # fix in place
lib/skyrim-mod-toolchain/scripts/format.sh --check modules   # report only
```

Linting (clang-tidy, [.clang-tidy](.clang-tidy)), after `scripts/test.sh` has configured `build/core`:

```sh
lib/skyrim-mod-toolchain/scripts/lint.sh build/core modules
```

## Contributing

Branches, commits and pull requests: [CONTRIBUTING.md](CONTRIBUTING.md).

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
