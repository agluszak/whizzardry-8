# Build against Ubuntu 24.04's glibc so the resulting executable can run on
# Ubuntu 24.04-based hosts, not just the newer CI runner.
FROM ubuntu:24.04@sha256:2e863c44b718727c860746568e1d54afd13b2fa71b160f5cd9058fc436217b30

ENV DEBIAN_FRONTEND=noninteractive

# Build-time system interfaces required by Conan's */system recipes.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates ccache cmake curl git gnupg ninja-build \
    pkg-config python3 python3-venv \
    libasound2-dev libegl-dev libgl-dev libudev-dev libvulkan-dev \
    libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev libxau-dev \
    libxaw7-dev libxcomposite-dev libxdamage-dev libxkbfile-dev libxmu-dev \
    libxmuu-dev libxpm-dev libxres-dev \
    libxcb-glx0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-xkb-dev \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev \
    libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-dri2-0-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-composite0-dev \
    libxcb-cursor-dev libxcb-ewmh-dev libxcb-res0-dev \
    mesa-vulkan-drivers xkb-data \
    && rm -rf /var/lib/apt/lists/*

# Clang 22 for Noble comes from the official LLVM APT repository.
RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --batch --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-22 main' \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-22 clang-format-22 clang-tidy-22 lld-22 \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/conan \
    && /opt/conan/bin/pip install --no-cache-dir 'conan==2.33.0'

ENV PATH="/opt/conan/bin:${PATH}" \
    CC=clang-22 \
    CXX=clang++-22

# Source/build trees and Conan caches are mounted, not copied into the image.
WORKDIR /work
