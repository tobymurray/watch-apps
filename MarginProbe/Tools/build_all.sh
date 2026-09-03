#!/usr/bin/env bash
# Build the whole series, one .uapp per inset, into Output/.
#
# Run it in the pinned toolchain container -- see the README. Each inset gets its
# own build directory, because they are eight different apps that happen to share
# a source tree, and a shared build/ would have them overwrite each other's
# objects and produce eight copies of whichever ran last.
set -euo pipefail

INSETS="${INSETS:-0 2 4 6 8 12 16 20}"
VERSION="${VERSION:-1.0.0}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$here/Software/Apps/MarginProbe-CMake"

for inset in $INSETS; do
    echo "=== inset $inset ==="
    cmake -B "build-$inset" -G "Unix Makefiles" \
        -DPROBE_INSET="$inset" -DBUILD_VERSION="$VERSION" . > "/tmp/probe-$inset.log" 2>&1 \
        || { tail -30 "/tmp/probe-$inset.log"; exit 1; }
    cmake --build "build-$inset" -j"$(nproc)" >> "/tmp/probe-$inset.log" 2>&1 \
        || { tail -40 "/tmp/probe-$inset.log"; exit 1; }
done

echo
echo "=== built ==="
ls -1 "$here/Output"/Edge*_"$VERSION".uapp
