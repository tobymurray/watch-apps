#!/usr/bin/env bash
#
# Build MagProbe, or its host tests, or the screen dumps -- in containers, so
# the result does not depend on what happens to be installed on this machine.
#
# **Two builds, not three.** The repo's rule is that all three builds have to
# pass before a branch is believed (SleepLab ledger row P14): the ARM build
# globs `Sources/*.cpp` while a TouchGFX simulator's Makefile enumerates them,
# and `timegm` links under glibc and does not exist in the watch's newlib. This
# app has no TouchGFX simulator to be the third, because its GUI goes through
# the CustomGUI entry point and links no TouchGFX at all. The disagreement that
# rule guards against is between a globbing build and an enumerating one, and
# with no enumerating build there is nothing to disagree. `screens` stands in
# for what the simulator gave: it renders every screen through the same
# `Render::render()` the firmware calls and writes the framebuffers out, so a
# layout can be looked at without a watch.
#
# The third thing that build rule guards against is floating-point `printf`:
# the host and the simulator link it, the watch's newlib may not, and when it
# does not `%f` emits nothing *at runtime* rather than failing at link time.
# Nothing in this app formats a float through printf -- see `Fmt.hpp` -- and no
# build can check that for you, which is why it is a design rule.
#
# Images are the ones SleepLab defines, unchanged:
#
#   docker build --platform linux/amd64 -t sleeplab-arm:latest  -f ../SleepLab/Tools/docker/arm.Dockerfile  ../SleepLab/Tools/docker
#   docker build                        -t sleeplab-host:latest -f ../SleepLab/Tools/docker/host.Dockerfile ../SleepLab/Tools/docker
#
# Override either with $MAGPROBE_{ARM,HOST}_IMAGE.
#
# Usage:
#   Tools/docker-build.sh app       # the .uapp
#   Tools/docker-build.sh tests     # host tests: configure, build, ctest
#   Tools/docker-build.sh screens   # render every screen to Output/screens/

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
APP_VERSION="${BUILD_VERSION:-0.1.0}"

ARM_IMAGE="${MAGPROBE_ARM_IMAGE:-sleeplab-arm:latest}"
HOST_IMAGE="${MAGPROBE_HOST_IMAGE:-sleeplab-host:latest}"

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
  app)
    run_arm "MagProbe/Software/Apps/MagProbe-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$APP_VERSION . && cmake --build build -j\$(nproc)"
    ;;
  tests)
    # The build directory lives inside the repo rather than in /tmp, because
    # each `docker run` is a fresh container and a /tmp build would not survive
    # between the configure and the ctest.
    run_host \
      "cmake -S MagProbe/Tests -B MagProbe/Tests/build -DCMAKE_BUILD_TYPE=Debug \
       && cmake --build MagProbe/Tests/build -j\$(nproc) \
       && cd MagProbe/Tests/build && ctest --output-on-failure"
    ;;
  screens)
    run_host \
      "cmake -S MagProbe/Tests -B MagProbe/Tests/build -DCMAKE_BUILD_TYPE=Debug \
       && cmake --build MagProbe/Tests/build --target magprobe-screens -j\$(nproc) \
       && mkdir -p MagProbe/Output/screens \
       && cd MagProbe/Output/screens && /w/MagProbe/Tests/build/magprobe-screens"
    ;;
  *)
    sed -n '2,40p' "$0"
    exit 2
    ;;
esac
