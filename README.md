# Trials of Mastery

A Skyrim AE mod package of SKSE plugins (stats, professions, mastery, luck). Goals, scope and the module split are in [docs/PRD.md](docs/PRD.md).

## Setup

On a Linux host, run once:

```sh
scripts/setup.sh
```

It checks the system tools and tells you what to install. It also sets up llvm-mingw, the CommonLibSSE-NG submodule, the xwin sysroot (MSVC CRT and Windows SDK) and vcpkg in your home directory. At the end it prints the environment variables to add to your shell profile (`VCPKG_ROOT`, `XWIN_SYSROOT`, `LLVM_MINGW_BIN`).

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
scripts/format.sh           # fix in place
scripts/format.sh --check   # report only
```

Linting (clang-tidy, [.clang-tidy](.clang-tidy)), after `scripts/test.sh` has configured `build/core`:

```sh
scripts/lint.sh
```

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
