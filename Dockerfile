FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

# Conan's */system dependencies come from this image, not the host.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates ccache curl git gnupg ninja-build \
    pkg-config python3-venv xz-utils unzip \
    g++-16 libstdc++-16-dev \
    libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth \
    libasound2-dev libegl-dev libgl-dev libudev-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev \
    libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev \
    libxau-dev libxaw7-dev libxcomposite-dev libxdamage-dev libxkbfile-dev \
    libxmu-dev libxmuu-dev libxpm-dev libxres-dev \
    libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev libxrender-dev \
    libxss-dev libxtst-dev libxv-dev libxxf86vm-dev \
    libxcb-glx0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-xkb-dev \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev \
    libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-dri2-0-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-composite0-dev \
    libxcb-cursor-dev libxcb-ewmh-dev libxcb-res0-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
      -o /usr/share/keyrings/llvm.asc \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.asc] https://apt.llvm.org/resolute/ llvm-toolchain-resolute-23 main" \
      > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      clang-23 clang-tidy-23 clang-format-23 \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/build-tools \
    && /opt/build-tools/bin/pip install --no-cache-dir \
      conan==2.33.0 cmake==4.4.4

ENV PATH="/opt/build-tools/bin:${PATH}" \
    CC=clang-23 CXX=clang++-23
