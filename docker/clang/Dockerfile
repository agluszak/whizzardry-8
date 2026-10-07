# syntax=docker/dockerfile:1@sha256:4edf897a3ffa55b89f906fc8cc78afdb3f1834cc9c7083565e611a8a7d5fe99e
FROM debian:trixie-20260824-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 AS toolchain

# Freeze Debian packages as well as the image, SDK manifest, and archives.
RUN rm /etc/apt/sources.list.d/debian.sources \
 && printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20260824T000000Z trixie main\n' > /etc/apt/sources.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
      clang-19 lld-19 llvm-19 cmake ninja-build python3 unzip \
 && rm -rf /var/lib/apt/lists/*
ENV PATH="/usr/lib/llvm-19/bin:${PATH}" WINSDK_ROOT=/opt/xwin

COPY vs-manifest.json /tmp/vs-manifest.json
RUN curl --connect-timeout 20 --max-time 120 -fsSL https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/xwin-0.10.0-x86_64-unknown-linux-musl.tar.gz -o /tmp/xwin.tar.gz \
 && echo 'd870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88  /tmp/xwin.tar.gz' | sha256sum -c - \
 && tar -xzf /tmp/xwin.tar.gz -C /tmp \
 && /tmp/xwin-0.10.0-x86_64-unknown-linux-musl/xwin --accept-license \
      --arch x86 --manifest /tmp/vs-manifest.json \
      splat --output /opt/xwin \
 && rm -rf /tmp/xwin* /tmp/vs-manifest.json /.xwin-cache

FROM toolchain AS product

RUN mkdir -p /opt/deps/zlib \
 && curl --connect-timeout 20 --max-time 120 -fsSL https://zlib.net/fossils/zlib-1.0.4.tar.gz -o /tmp/zlib.tar.gz \
 && echo 'e5c260cd3db1370fb3e0c193e9cbd9f127a9bd055d622b3fb55b82747f6e5b24  /tmp/zlib.tar.gz' | sha256sum -c - \
 && tar -xzf /tmp/zlib.tar.gz -C /opt/deps/zlib --strip-components=1 \
 && rm /tmp/zlib.tar.gz

RUN ln -s /usr/lib/llvm-19/bin/clang /usr/local/bin/clang-cl

WORKDIR /repo
CMD ["sh", "-c", "cmake -S /repo -B /out -G Ninja -DCMAKE_TOOLCHAIN_FILE=/repo/cmake/clang-cl-i686.cmake -DZLIB_SOURCE=/opt/deps/zlib && cmake --build /out --parallel ${BUILD_JOBS:-8}"]
