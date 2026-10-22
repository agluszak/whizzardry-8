#!/bin/bash
# Claude Code cloud environment setup script for whizzardry-8.
# Paste this into the environment's Setup script field. It installs the toolchain
# that tools/setup-linux.sh expects (Clang 23, GCC 16's libstdc++, Ninja, ccache,
# Vulkan/Mesa/Xvfb, distribution glslang, Conan 2.33.0 and CMake 4.4.4) so the
# environment snapshot carries it and threads skip straight to the Conan install.
# It must finish in about five minutes for the snapshot to be kept, so it does
# not build the Conan dependencies.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
install -d -m 0755 /etc/apt/keyrings

curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key >/etc/apt/keyrings/apt.llvm.org.asc &
curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x60C317803A41BA51845E371A1E9377A2BA9EF27F' \
    >/etc/apt/keyrings/ubuntu-toolchain-r.asc &
wait
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] http://apt.llvm.org/$codename/ llvm-toolchain-$codename-23 main" \
    >/etc/apt/sources.list.d/llvm-23.list
echo "deb [signed-by=/etc/apt/keyrings/ubuntu-toolchain-r.asc] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu $codename main" \
    >/etc/apt/sources.list.d/ubuntu-toolchain-r.list

# pip does not touch apt, so it runs alongside the package install.
python3 -m pip install --quiet --break-system-packages 'conan==2.33.0' 'cmake==4.4.4' &
pip_job=$!

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    clang-23 clang-format-23 clang-tidy-23 g++-16 \
    ninja-build ccache libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth \
    glslang-tools glslang-dev libasound2-dev \
    libegl-dev libgl-dev libgles-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libxfixes-dev \
    libwayland-dev libdrm-dev libgbm-dev libpulse-dev libudev-dev libdbus-1-dev pkg-config
wait "$pip_job"
hash -r

# Let Conan recipes install any further system packages they ask for.
conan profile path default >/dev/null 2>&1 || conan profile detect >/dev/null
grep -qs '^tools.system.package_manager:mode' "$(conan config home)/global.conf" ||
    echo 'tools.system.package_manager:mode=install' >>"$(conan config home)/global.conf"

clang++-23 --version | head -1
g++-16 --version | head -1
conan --version
cmake --version | head -1
