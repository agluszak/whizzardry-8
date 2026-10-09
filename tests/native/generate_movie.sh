#!/usr/bin/env bash
# Authored lavfi fixture; no retail movie bytes are redistributed.
set -euo pipefail
cd "$(dirname "$0")"
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=32x24:rate=10:duration=0.5' \
    -f lavfi -i 'sine=frequency=440:sample_rate=44100:duration=0.5' \
    -c:v ffv1 -c:a pcm_s16le -ac 2 -map_metadata -1 \
    -fflags +bitexact -flags:v +bitexact -flags:a +bitexact movie.mkv
ffmpeg -v error -y -i movie.mkv -an -sws_flags bilinear+bitexact \
    -pix_fmt rgb555le -f rawvideo movie.rgb555
