#!/usr/bin/env bash
# Prepare an Ubuntu host to build and test, in one step:
#   tools/setup-linux.sh
# installs Clang 23 (clang, clang-format, clang-tidy), GCC 16's libstdc++, Ninja, the
# Vulkan loader, Mesa's software driver, Conan 2 and CMake 4.4.4; resolves the
# Conan dependencies; and configures build/linux. Then:
#   . build/linux/conan/conanbuild.sh && cmake --build --preset linux
#   . build/linux/conan/conanrun.sh && ctest --preset linux
# It is safe to run again; installed pieces are skipped.
#
# Networks that serve git but not GitHub source archives can set:
#   WIZ8_SYSTEM_GLSLANG=1  use the distribution's glslang instead of Conan's
#   WIZ8_GIT_SOURCES=1     clone the archive-fetched sources (utf8proc) with git
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
sudo=$([[ $(id -u) == 0 ]] && echo "" || echo sudo)
conan_version=2.33.0
cmake_version=4.4.4
utf8proc_tag=v2.10.0

apt_install() {
    local missing=()
    for package in "$@"; do
        dpkg -s "$package" >/dev/null 2>&1 || missing+=("$package")
    done
    if ((${#missing[@]})); then
        $sudo apt-get install -y --no-install-recommends "${missing[@]}"
    fi
}

if ! command -v clang++-23 >/dev/null || ! command -v clang-format-23 >/dev/null ||
    ! command -v clang-tidy-23 >/dev/null; then
    tools/install-llvm.sh 23 clang clang-format clang-tidy
fi

# GCC 16's libstdc++ is in Ubuntu 26.04; older releases take it from the toolchain PPA.
if ! command -v g++-16 >/dev/null; then
    $sudo apt-get update
    # apt-cache show treats its argument as a pattern and matches clang-16; ask for a candidate.
    if ! apt-cache policy g++-16 | grep -q 'Candidate: [0-9]'; then
        codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
        keyring=/etc/apt/keyrings/ubuntu-toolchain-r.asc
        $sudo install -d -m 0755 /etc/apt/keyrings
        curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x60C317803A41BA51845E371A1E9377A2BA9EF27F' |
            $sudo tee "$keyring" >/dev/null
        echo "deb [signed-by=$keyring] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu $codename main" |
            $sudo tee /etc/apt/sources.list.d/ubuntu-toolchain-r.list >/dev/null
        $sudo apt-get update
    fi
    apt_install g++-16
fi

apt_install ninja-build ccache python3-pip libvulkan-dev mesa-vulkan-drivers \
    vulkan-validationlayers
if [[ ${WIZ8_SYSTEM_GLSLANG:-} == 1 ]]; then
    apt_install glslang-tools glslang-dev
fi

if [[ $(conan --version 2>/dev/null) != *"$conan_version"* ]] ||
    [[ $(cmake --version 2>/dev/null) != *"$cmake_version"* ]]; then
    python3 -m pip install --quiet --break-system-packages \
        "conan==$conan_version" "cmake==$cmake_version" ||
        python3 -m pip install --quiet "conan==$conan_version" "cmake==$cmake_version"
    hash -r
fi

conan profile path default >/dev/null 2>&1 || conan profile detect >/dev/null
conan export conan/recipes/libalsa >/dev/null

conanfile=.
conan_extra=()
cmake_extra=()
if [[ ${WIZ8_SYSTEM_GLSLANG:-} == 1 ]]; then
    # The same graph without glslang, as a library or a tool; CMake finds the system one.
    conanfile=build/linux/conanfile-system-glslang
    mkdir -p "$conanfile"
    sed -e '/self.requires("glslang\//d' -e '/def build_requirements/,+2d' conanfile.py \
        >"$conanfile/conanfile.py"
    cp conan.lock "$conanfile/"
    conan_extra+=(--lockfile-partial)
    cmake_extra+=(-Dglslang_DIR=/usr/lib/x86_64-linux-gnu/cmake/glslang)
fi
if [[ ${WIZ8_GIT_SOURCES:-} == 1 ]]; then
    utf8proc=build/sources/utf8proc
    [[ -d $utf8proc/.git ]] ||
        git clone --quiet --depth 1 --branch "$utf8proc_tag" https://github.com/JuliaStrings/utf8proc "$utf8proc"
    cmake_extra+=(-DFETCHCONTENT_SOURCE_DIR_UTF8PROC="$root/$utf8proc")
fi

# Recipes such as libalsa and SDL's X11/Wayland dependencies install their system packages.
conan_extra+=(-c tools.system.package_manager:mode=install)
[[ -z $sudo ]] || conan_extra+=(-c tools.system.package_manager:sudo=True)

CC=clang-23 CXX=clang++-23 conan install "$conanfile" \
    -pr:h conan/profiles/linux-clang23 -pr:b conan/profiles/linux-gcc16 \
    -s:h build_type=Release --build=missing -of build/linux/conan "${conan_extra[@]}"

# shellcheck disable=SC1091
. build/linux/conan/conanbuild.sh
cmake --preset linux \
    -DCMAKE_TOOLCHAIN_FILE="$root/build/linux/conan/conan_toolchain.cmake" \
    -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23 "${cmake_extra[@]}"
echo "Configured build/linux."
