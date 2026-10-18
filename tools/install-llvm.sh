#!/usr/bin/env bash
# Install versioned LLVM packages from apt.llvm.org on Ubuntu, e.g.
#   tools/install-llvm.sh 23 clang clang-tidy
set -euo pipefail

version=$1
shift
codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
keyring=/etc/apt/keyrings/apt.llvm.org.asc

sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee "$keyring" >/dev/null
echo "deb [signed-by=$keyring] http://apt.llvm.org/$codename/ llvm-toolchain-$codename-$version main" \
    | sudo tee "/etc/apt/sources.list.d/llvm-$version.list" >/dev/null
sudo apt-get update
sudo apt-get install -y "${@/%/-$version}"
