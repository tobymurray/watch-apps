#!/usr/bin/env bash
#
# Build SensorLab, or its host tests, or the TouchGFX simulator -- in
# containers, so the result does not depend on what happens to be installed on
# this machine.
#
# **All three builds have to be run before a branch is believed** (SleepLab's
# ledger row P14, confirmed twice). They do not agree on which sources exist or
# which libc functions do:
#
#   * the ARM build globs `Sources/*.cpp` while the simulator's Makefile
#     enumerates them, so a new source packs into a `.uapp` and fails to link
#     the simulator;
#   * `timegm` links under the host tests and the simulator (both glibc) and does
#     not exist in the watch's newlib, so a date routine can compile everywhere
#     except the build that ships.
#
# SensorLab adds a third disagreement of its own: floating-point `printf`. The
# host and the simulator link it; the watch's newlib may not, and when it does
# not `%f` emits nothing *at runtime* rather than failing at link time. Nothing
# in this app formats a float -- see `Profile/Decimal.hpp` -- and no build can
# check that for you, which is why it is a design rule rather than a test.
#
# Three images, because they are three different jobs. They are the same images
# SleepLab uses, and the definitions live there:
#
#   arm    ghcr.io/tobymurray/kira-toolchain, plus pyelftools and Pillow for
#          `app_packer.py`. This is the image Kira publishes binaries with, so a
#          .uapp built here is the same artifact the catalogue would carry.
#          amd64 only, so it runs emulated on Apple silicon -- slow, correct.
#   host   cmake, a C++17 g++, and python3 -- the last of those so the
#          generated-table check and the report round trip run the real scripts
#          rather than skipping.
#   sim    the TouchGFX Linux simulator, which needs SDL2 *and* Ruby (its asset
#          generators are Ruby scripts) *and* amd64.
#
# Usage:
#   Tools/docker-build.sh app       # the .uapp
#   Tools/docker-build.sh tests     # host tests: configure, build, ctest
#   Tools/docker-build.sh sim       # build the simulator
#   Tools/docker-build.sh sim-run   # build it and run it headless
#   Tools/docker-build.sh catalogue # regenerate the sensor type table
#
# Build the images from SleepLab's definitions:
#
#   docker build --platform linux/amd64 -t sleeplab-arm:latest  -f ../SleepLab/Tools/docker/arm.Dockerfile  ../SleepLab/Tools/docker
#   docker build                        -t sleeplab-host:latest -f ../SleepLab/Tools/docker/host.Dockerfile ../SleepLab/Tools/docker
#   docker build --platform linux/amd64 -t sleeplab-sim:latest  -f ../SleepLab/Tools/docker/sim.Dockerfile  ../SleepLab/Tools/docker
#
# Override any of them with $SENSORLAB_{ARM,HOST,SIM}_IMAGE.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
APP_VERSION="${BUILD_VERSION:-0.1.0}"

ARM_IMAGE="${SENSORLAB_ARM_IMAGE:-sleeplab-arm:latest}"
HOST_IMAGE="${SENSORLAB_HOST_IMAGE:-sleeplab-host:latest}"
SIM_IMAGE="${SENSORLAB_SIM_IMAGE:-sleeplab-sim:latest}"

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
        -w /w/SensorLab/Software/Apps/TouchGFX-GUI \
        "$SIM_IMAGE" bash -lc "$1"
}

case "${1:-}" in
  app)
    run_arm "SensorLab/Software/Apps/SensorLab-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$APP_VERSION . && cmake --build build -j\$(nproc)"
    ;;
  tests)
    # The build directory lives inside the repo rather than in /tmp, because each
    # `docker run` is a fresh container and a /tmp build would not survive
    # between the configure and the ctest. `build/` is gitignored at the root.
    run_host \
      "cmake -S SensorLab/Tests -B SensorLab/Tests/build -DCMAKE_BUILD_TYPE=Debug -DBUILD_VERSION=$APP_VERSION \
       && cmake --build SensorLab/Tests/build -j\$(nproc) \
       && cd SensorLab/Tests/build && ctest --output-on-failure"
    ;;
  catalogue)
    # Regenerate `SensorTypeTable.generated.hpp` from the SDK headers. Run this
    # after pointing $UNA_SDK at a new SDK, and commit the result: the
    # `sensorlab-catalogue-current` ctest is what notices you have not.
    run_host \
      "python3 SensorLab/Tools/gen_catalogue.py \
         --out SensorLab/Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp"
    ;;
  sim)
    run_sim "make -f simulator/gcc/Makefile -j\$(nproc)"
    ;;
  sim-run)
    # Headless. **The simulator has four sensor sources -- battery level,
    # GPS/step counter, heart rate and pressure -- so it can exercise the
    # screens, the statistics and the report writer, and it can tell you nothing
    # whatever about a sensor.** Its sample-rate adapter also thins delivery on a
    # half-period boundary in quantised bands, which the hardware demonstrably
    # does not do (ledger row S3). Nothing in `Docs/LEDGER.md` is ever sourced
    # from a simulator run.
    #
    # Its shutdown path calls a pure virtual method (row T6, fixed on
    # `una-sdk`'s `fix/simulator-shutdown-pure-virtual`), so a run's exit status
    # says nothing -- which is why this pipes through `head` and does not check
    # one.
    run_sim "make -f simulator/gcc/Makefile -j\$(nproc) >/dev/null \
        && mkdir -p /tmp/a/b/c/d/e \
        && cd /tmp/a/b/c/d/e \
        && SDL_VIDEODRIVER=dummy timeout \${SIM_SECONDS:-12} \
             /w/SensorLab/Software/Apps/TouchGFX-GUI/build/bin/simulator.out 2>&1 | head -60"
    ;;
  *)
    sed -n '2,50p' "$0"
    exit 2
    ;;
esac
