#!/usr/bin/env bash
#
# Build SleepLab, or its Tier 0 probe, or the host tests -- in containers, so
# the result does not depend on what happens to be installed on this machine.
#
# Two images, because they are two different jobs:
#
#   arm    ghcr.io/tobymurray/kira-toolchain, plus pyelftools and Pillow for
#          `app_packer.py`. This is the image Kira publishes binaries with, so
#          a .uapp built here is the same artifact the catalogue would carry.
#          amd64 only, so it runs emulated on Apple silicon -- slow, correct.
#   host   cmake, a C++17 g++, and python3 -- the last of those so the probe
#          report round-trip test runs the real script rather than skipping.
#
# Usage:
#   Tools/docker-build.sh probe            # the Tier 0 probe .uapp
#   Tools/docker-build.sh app              # the SleepLab .uapp
#   Tools/docker-build.sh tests            # host tests, configure + build + ctest
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
VERSION="${BUILD_VERSION:-0.1.0}"

ARM_IMAGE="${SLEEPLAB_ARM_IMAGE:-sleeplab-arm:latest}"
HOST_IMAGE="${SLEEPLAB_HOST_IMAGE:-sleeplab-host:latest}"

run_arm() {
    docker run --rm --platform linux/amd64 \
        -v "$REPO:/w" -v "$SDK:/sdk" -e UNA_SDK=/sdk -w "/w/$1" \
        "$ARM_IMAGE" bash -lc "$2"
}

run_host() {
    docker run --rm \
        -v "$REPO:/w" -v "$SDK:/sdk" -e UNA_SDK=/sdk -w /w \
        "$HOST_IMAGE" bash -lc "$1"
}

case "${1:-}" in
  probe)
    run_arm "SleepLab/Probe/Software/App/SleepProbe-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$VERSION . && cmake --build build -j\$(nproc)"
    ;;
  app)
    run_arm "SleepLab/Software/Apps/SleepLab-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$VERSION . && cmake --build build -j\$(nproc)"
    ;;
  tests)
    run_host \
      "cmake -S SleepLab/Tests -B /tmp/slt -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/slt -j\$(nproc) && cd /tmp/slt && ctest --output-on-failure"
    ;;
  *)
    sed -n '2,30p' "$0"
    exit 2
    ;;
esac
