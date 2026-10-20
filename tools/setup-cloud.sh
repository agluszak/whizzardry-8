#!/usr/bin/env bash
# Provision a Claude Code cloud environment (Ubuntu 24.04, root) with the
# toolchain from native.yml and prebuilt Conan dependencies, so sessions can
# configure and build immediately. Use it as the environment's setup script:
#   bash tools/setup-cloud.sh
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export DEBIAN_FRONTEND=noninteractive

"$repo_dir/tools/install-llvm.sh" 23 clang clang-tidy

# Noble has no GCC 16; libstdc++ 16 comes from the toolchain test PPA.
toolchain_keyring=/etc/apt/keyrings/ubuntu-toolchain-r.asc
curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x60C317803A41BA51845E371A1E9377A2BA9EF27F' \
    -o "$toolchain_keyring"
echo "deb [signed-by=$toolchain_keyring] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu noble main" \
    > /etc/apt/sources.list.d/ubuntu-toolchain-r.list
apt-get update
apt-get install -y g++-16 ninja-build ccache mesa-vulkan-drivers innoextract

# ~/.local/bin precedes /usr/bin on the session PATH.
python3 -m pip install --user 'conan==2.33.0' 'cmake==4.4.4'
export PATH="$HOME/.local/bin:$PATH"

cd "$repo_dir"
conan profile detect --exist-ok
# The session proxy blocks GitHub archives outside the attached repository;
# ConanCenter mirrors every recipe source by checksum.
global_conf="$(conan config home)/global.conf"
grep -q '^core.sources:download_urls' "$global_conf" 2>/dev/null ||
    echo 'core.sources:download_urls=["origin", "https://c3i.jfrog.io/artifactory/conan-center-backup-sources/"]' \
        >> "$global_conf"
conan export conan/recipes/libalsa
CC=clang-23 CXX=clang++-23 conan install . \
    -pr:h conan/profiles/linux-clang23 -pr:b conan/profiles/linux-gcc16 \
    -s:h build_type=Debug -c tools.system.package_manager:mode=install \
    --build=missing -of build/linux/conan
conan cache clean '*'
