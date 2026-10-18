# Whizzardry 8

Whizzardry 8 is a work-in-progress port of [Wizardry 8](https://en.wikipedia.org/wiki/Wizardry_8) to modern, 64-bit Linux, macOS and Windows.

It builds on the recovered source code from [Wizardry 8 Decomp](https://github.com/agluszak/wizardry-8-decomp), but has a different goal: **keep the game, replace the outdated machinery**. Instead of reproducing the original executable, we're simplifying the code and using C++23, SDL3 and other cross-platform libraries.

Linux is the only platform currently tested in CI. macOS and Windows builds are not yet verified, and the game is still under development.

This repository does **not** include game assets. You'll need your own retail copy of Wizardry 8.

## Build on Linux

You'll need Clang 23, CMake, Ninja, Python 3, [Conan 2](https://conan.io/) and a working Vulkan driver. Conan handles the C/C++ library dependencies.

From the repository root:

```sh
python3 -m pip install 'conan==2.33.0'
conan profile detect
conan export conan/recipes/libalsa

CC=clang-23 CXX=clang++-23 conan install . \
    -pr:h conan/profiles/linux-clang23 -pr:b default \
    -s:h build_type=Release --build=missing -of build/linux/conan

. build/linux/conan/conanbuild.sh
cmake --preset linux \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/build/linux/conan/conan_toolchain.cmake" \
    -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
cmake --build --preset linux
. build/linux/conan/conanrun.sh
```

The first build may take a while because Conan needs to build some dependencies.

## Play

First, prepare the data from your retail installation:

```sh
build/linux/wiz8_prepare_assets /path/to/prepared-assets /path/to/retail-install
```

Choose a **new directory** for `prepared-assets`, outside the retail installation. The preparation tool extracts the game data and applies the installed patches.

Then launch the game:

```sh
WIZ8_ASSET_ROOT=/path/to/prepared-assets ./run.sh
```

Saves, settings and logs go into the system's per-user application directory, separate from the game assets. Set `WIZ8_USER_ROOT` if you'd rather choose that directory yourself.

## Contributing

This is a modernization project, not an exercise in preserving the original engine's structure. We aim to remove obsolete code and unnecessary abstractions while keeping Wizardry 8's gameplay, content and file compatibility.

For the current technical direction, see [AGENTS.md](AGENTS.md), [remaining native work](NATIVE_WORK.md) and [UI modernization](UI_MODERNIZATION.md).

After building, run the tests with:

```sh
ctest --preset linux
```

Some runtime tests also need prepared retail assets:

```sh
WIZ8_ASSET_ROOT=/path/to/prepared-assets ctest --preset linux -L runtime
```

C and C++ changes should use the repository's `.clang-format` configuration (clang-format 23).
