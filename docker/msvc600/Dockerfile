# syntax=docker/dockerfile:1
FROM debian:trixie-20260824-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 AS toolchain

# Use QEMU only on hosts that cannot execute Linux i386 ELF binaries.
ARG WIZ8_EMULATE_I386=0
COPY wine-runner.sh /usr/local/bin/wiz8-wine
COPY qemu-wine-preloader.sh /usr/local/libexec/qemu-wine-preloader

ARG MSVC_REPOSITORY=https://github.com/archaic-msvc/msvc600_sp5.git
ARG MSVC_REF=b0b07e29108e2695eb0274c2a377a7b7d7326150

RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      git \
      unzip \
      wget \
      wine \
      wine32 \
 && if [ "${WIZ8_EMULATE_I386}" = 1 ]; then \
      apt-get install -y --no-install-recommends qemu-user wine64 wine32-preloader; \
      mv /usr/lib/wine/wine-preloader /usr/lib/wine/wine-preloader.real; \
      install -m 0755 /usr/local/libexec/qemu-wine-preloader /usr/lib/wine/wine-preloader; \
    fi \
 && chmod +x /usr/local/bin/wiz8-wine \
 && rm -rf /var/lib/apt/lists/*

ENV WIZ8_EMULATE_I386=${WIZ8_EMULATE_I386} \
    WINEARCH=win32 \
    WINEDEBUG=-all \
    WINEPREFIX=/root/.wine \
    INCLUDE="C:\\msvc\\VC98\\Include;C:\\msvc\\VC98\\MFC\\Include;C:\\msvc\\VC98\\ATL\\Include" \
    LIB="C:\\msvc\\VC98\\Lib;C:\\msvc\\VC98\\MFC\\Lib" \
    COMSPEC="C:\\windows\\system32\\cmd.exe" \
    TMP="Z:\\out\\tmp" \
    TEMP="Z:\\out\\tmp" \
    WINEPATH="C:\\cmake\\bin;C:\\msvc\\Common\\MSDev98\\Bin;C:\\msvc\\VC98\\Bin"

# Provision the complete pinned prefix in one layer. VFS duplicates the
# parent tree per layer; separate Wine/compiler/CMake/JOM steps exhaust small
# analysis workspaces before the final image can be exported. Stop wineserver
# and remove its Unix sockets before snapshotting the layer.
RUN --mount=type=secret,id=proxy_ca,env=WIZ8_PROXY_CA \
    /usr/local/bin/wiz8-wine wineboot --init \
 && if [ "${WIZ8_EMULATE_I386}" = 1 ]; then \
      /usr/lib/wine/wineserver64 -k; /usr/lib/wine/wineserver64 -w; \
    else \
      wineserver -k; wineserver -w; \
    fi \
 && rm -rf /tmp/.wine-0 \
 && if [ -n "${WIZ8_PROXY_CA:-}" ]; then printf "%s\n" "$WIZ8_PROXY_CA" > /tmp/proxy-ca.pem; export GIT_SSL_CAINFO=/tmp/proxy-ca.pem; fi \
 && git init /root/.wine/drive_c/msvc \
 && git -C /root/.wine/drive_c/msvc remote add origin "${MSVC_REPOSITORY}" \
 && git -C /root/.wine/drive_c/msvc fetch --depth 1 origin "${MSVC_REF}" \
 && git -C /root/.wine/drive_c/msvc checkout --detach FETCH_HEAD \
 && test "$(git -C /root/.wine/drive_c/msvc rev-parse HEAD)" = "${MSVC_REF}" \
 && rm -rf /root/.wine/drive_c/msvc/.git \
 && rm -f /tmp/proxy-ca.pem \
 && if [ -n "${WIZ8_PROXY_CA:-}" ]; then printf "%s\n" "$WIZ8_PROXY_CA" > /tmp/proxy-ca.pem; export WGETRC=/tmp/wgetrc; printf "ca_certificate = /tmp/proxy-ca.pem\n" > "$WGETRC"; fi \
 && wget -O /tmp/cmake.zip \
      https://github.com/Kitware/CMake/releases/download/v3.26.6/cmake-3.26.6-windows-i386.zip \
 && echo "dc3f535d880d1ffa7cfa86a0eefdc1fc9bf8b9ede59d1deb7a2bb41f0a58d2d4  /tmp/cmake.zip" \
      | sha256sum -c - \
 && unzip /tmp/cmake.zip -d /root/.wine/drive_c \
 && mv /root/.wine/drive_c/cmake-3.26.6-windows-i386 /root/.wine/drive_c/cmake \
 && rm -f /tmp/cmake.zip /tmp/wgetrc /tmp/proxy-ca.pem \
 && if [ -n "${WIZ8_PROXY_CA:-}" ]; then printf "%s\n" "$WIZ8_PROXY_CA" > /tmp/proxy-ca.pem; export WGETRC=/tmp/wgetrc; printf "ca_certificate = /tmp/proxy-ca.pem\n" > "$WGETRC"; fi \
 && wget -O /tmp/jom.zip \
      https://download.qt.io/official_releases/jom/jom_1_1_3.zip \
 && echo "128fdd846fe24f8594eed37d1d8929a0ea78df563537c0c1b1861a635013fff8  /tmp/jom.zip" \
      | sha256sum -c - \
 && mkdir -p /root/.wine/drive_c/jom \
 && unzip /tmp/jom.zip -d /root/.wine/drive_c/jom \
 && rm -f /tmp/jom.zip /tmp/wgetrc /tmp/proxy-ca.pem

# Product builds only need Wine plus the already-provisioned VC6/CMake/JOM
# prefix. Keep Linux analysis/build packages out of this image.
FROM debian:trixie-20260824-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 AS product

ARG WIZ8_EMULATE_I386=0
COPY wine-runner.sh /usr/local/bin/wiz8-wine
COPY qemu-wine-preloader.sh /usr/local/libexec/qemu-wine-preloader

RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      wine \
      wine32 \
 && if [ "${WIZ8_EMULATE_I386}" = 1 ]; then \
      apt-get install -y --no-install-recommends qemu-user wine64 wine32-preloader; \
      mv /usr/lib/wine/wine-preloader /usr/lib/wine/wine-preloader.real; \
      install -m 0755 /usr/local/libexec/qemu-wine-preloader /usr/lib/wine/wine-preloader; \
    fi \
 && chmod +x /usr/local/bin/wiz8-wine \
 && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /root/.wine /root/.wine

ENV WIZ8_EMULATE_I386=${WIZ8_EMULATE_I386} \
    WINEARCH=win32 \
    WINEDEBUG=-all \
    WINEPREFIX=/root/.wine \
    INCLUDE="C:\\msvc\\VC98\\Include;C:\\msvc\\VC98\\MFC\\Include;C:\\msvc\\VC98\\ATL\\Include" \
    LIB="C:\\msvc\\VC98\\Lib;C:\\msvc\\VC98\\MFC\\Lib" \
    COMSPEC="C:\\windows\\system32\\cmd.exe" \
    TMP="Z:\\out\\tmp" \
    TEMP="Z:\\out\\tmp" \
    WINEPATH="C:\\cmake\\bin;C:\\msvc\\Common\\MSDev98\\Bin;C:\\msvc\\VC98\\Bin"

ENTRYPOINT ["/usr/local/bin/wiz8-wine"]
