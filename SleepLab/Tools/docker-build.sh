#!/usr/bin/env bash
#
# Build SleepLab, or its Tier 0 probe, or the host tests -- in containers, so
# the result does not depend on what happens to be installed on this machine.
#
# Three images, because they are three different jobs:
#
#   arm    ghcr.io/tobymurray/kira-toolchain, plus pyelftools and Pillow for
#          `app_packer.py`. This is the image Kira publishes binaries with, so
#          a .uapp built here is the same artifact the catalogue would carry.
#          amd64 only, so it runs emulated on Apple silicon -- slow, correct.
#   host   cmake, a C++17 g++, and python3 -- the last of those so the probe
#          report round-trip test runs the real script rather than skipping.
#   sim    the TouchGFX Linux simulator, which needs SDL2 *and* Ruby (its asset
#          generators are Ruby scripts) *and* amd64 (some of them are amd64-only
#          binaries). So it is layered on the amd64 base rather than the native
#          one, and runs emulated on Apple silicon.
#
# Usage:
#   Tools/docker-build.sh probe            # the Tier 0 probe .uapp
#   Tools/docker-build.sh app              # the SleepLab .uapp
#   Tools/docker-build.sh tests            # host tests, configure + build + ctest
#   Tools/docker-build.sh sim              # build the simulator
#   Tools/docker-build.sh sim-run          # build it and run it headless
#
# The three images are built from Tools/docker/*.Dockerfile:
#
#   docker build --platform linux/amd64 -t sleeplab-arm:latest -f Tools/docker/arm.Dockerfile Tools/docker
#   docker build                        -t sleeplab-host:latest -f Tools/docker/host.Dockerfile Tools/docker
#   docker build --platform linux/amd64 -t sleeplab-sim:latest  -f Tools/docker/sim.Dockerfile  Tools/docker
#
# Override any of them with $SLEEPLAB_{ARM,HOST,SIM}_IMAGE.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
# The two apps version independently, and conflating them was a real hazard: one
# `BUILD_VERSION` meant bumping SleepLab silently restamped the probe, which is
# published at 0.1.1 in Kira and has not changed since. `BUILD_VERSION` still
# overrides either, for a one-off build.
APP_VERSION="${BUILD_VERSION:-0.3.0}"
PROBE_VERSION="${BUILD_VERSION:-0.1.1}"

ARM_IMAGE="${SLEEPLAB_ARM_IMAGE:-sleeplab-arm:latest}"
HOST_IMAGE="${SLEEPLAB_HOST_IMAGE:-sleeplab-host:latest}"
SIM_IMAGE="${SLEEPLAB_SIM_IMAGE:-sleeplab-sim:latest}"

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

run_sim() {
    docker run --rm --platform linux/amd64 \
        -v "$REPO:/w" -v "$SDK:/sdk" -e UNA_SDK=/sdk \
        -w /w/SleepLab/Software/Apps/TouchGFX-GUI \
        "$SIM_IMAGE" bash -lc "$1"
}

case "${1:-}" in
  probe)
    run_arm "SleepLab/Probe/Software/App/SleepProbe-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$PROBE_VERSION . && cmake --build build -j\$(nproc)"
    ;;
  app)
    run_arm "SleepLab/Software/Apps/SleepLab-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$APP_VERSION . && cmake --build build -j\$(nproc)"
    ;;
  tests)
    run_host \
      "cmake -S SleepLab/Tests -B /tmp/slt -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/slt -j\$(nproc) && cd /tmp/slt && ctest --output-on-failure"
    ;;
  sim)
    run_sim "make -f simulator/gcc/Makefile -j\$(nproc)"
    ;;
  sim-run)
    # Headless, with a seeded history so the report and history screens have
    # something to draw. The simulator has no sensors and no battery, so this
    # exercises the screen, the message contract and the file reading -- and
    # proves nothing whatever about whether an eight-hour recording survives on
    # hardware. That is what the Tier 0 probe is for.
    run_sim "make -f simulator/gcc/Makefile -j\$(nproc) >/dev/null \
        && mkdir -p /tmp/a/b/c/d/e /tmp/Output/Nights \
        && cp /w/SleepLab/Tests/fixtures/index.csv /tmp/Output/Nights/ \
        && cd /tmp/a/b/c/d/e \
        && SDL_VIDEODRIVER=dummy timeout \${SIM_SECONDS:-12} \
             /w/SleepLab/Software/Apps/TouchGFX-GUI/build/bin/simulator.out 2>&1 | head -40"
    ;;
  *)
    sed -n '2,34p' "$0"
    exit 2
    ;;
esac
