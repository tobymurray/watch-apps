#!/usr/bin/env bash
#
# Build and check Squash in containers, so the result does not depend on what
# happens to be installed on this machine. Modelled on SleepLab/Tools/docker-build.sh,
# which owns the explanation of the three images and how they are built.
#
# Usage:
#   Tools/docker-build.sh app        # the .uapp
#   Tools/docker-build.sh tests      # host tests, configure + build + ctest
#   Tools/docker-build.sh sim        # build the TouchGFX simulator
#   Tools/docker-build.sh rust       # EffortKit: tests, lint, and the watch target
#
# THE SDK THIS NEEDS
#
# Two requirements, and getting either wrong looks like a bug in this tree:
#
#   * AppConfig. Squash reads input.json through SDK::AppConfig, which exists on
#     the SDK's main line and NOT on the apps-v1.4.0 tag. Building the simulator
#     against the tag fails on a missing SDK/AppConfig/AppConfig.hpp.
#   * ImuFusionSource. squash-filesink-tests replays what landed in storage back
#     through the simulator's IMU fusion source, and the simulator itself needs
#     it to play a recorded CSV. It is one commit on the SDK's simulator-imu-source
#     branch, which is not on the main line.
#
# So $UNA_SDK must point at a checkout carrying both. Build one with:
#
#   cd "$SDK_REPO"
#   git worktree add .claude/worktrees/imu-source -b sim/imu-source-pinned \
#       "$(sed -n 's/.*SDK_REF: //p' ../watch-apps/.github/workflows/app-build.yml)"
#   git -C .claude/worktrees/imu-source cherry-pick 5df2033e
#
# The SHA is the "feat(simulator): add IMU fusion (FUSION_RAW) sensor source"
# commit; the branch it sits on is 46 commits behind the revision CI pins, which
# is why it is cherry-picked onto that revision rather than used as it stands.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
VERSION="${BUILD_VERSION:-0.1.0}"

ARM_IMAGE="${SQUASH_ARM_IMAGE:-sleeplab-arm:latest}"
HOST_IMAGE="${SQUASH_HOST_IMAGE:-sleeplab-host:latest}"
SIM_IMAGE="${SQUASH_SIM_IMAGE:-sleeplab-sim:latest}"

# Resolved on demand, not at the top: the `rust` target needs no SDK at all, and
# failing it on a missing one sends the reader looking in the wrong place.
need_sdk() {
    SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" 2>/dev/null && pwd)}"
    if [ -z "${SDK:-}" ] || [ ! -f "$SDK/Libs/Header/SDK/AppConfig/AppConfig.hpp" ]; then
        echo "error: \$UNA_SDK (${SDK:-unset}) has no SDK/AppConfig — see the header of this script" >&2
        exit 1
    fi
}

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
        -w /w/Squash/Software/Apps/TouchGFX-GUI \
        "$SIM_IMAGE" bash -lc "$1"
}

case "${1:-}" in
  app)
    need_sdk
    run_arm "Squash/Software/Apps/Squash-CMake" \
      "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$VERSION . && cmake --build build -j\$(nproc)"
    ;;
  tests)
    need_sdk
    # Configure, build and run in one container: /tmp does not survive between
    # docker run invocations, so a split would silently rebuild from scratch or
    # fail to find the binaries.
    run_host \
      "cmake -S Squash/Tests -B /tmp/sq -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/sq -j\$(nproc) && cd /tmp/sq && ctest --output-on-failure"
    ;;
  sim)
    need_sdk
    # The simulator image has no cargo and the arm image does, so the engine's
    # host-target archive is built there first. Both images are linux/amd64, so
    # the archive one produces is the one the other links.
    run_arm "Squash/Software/Libs/rust" "cargo build --release"
    run_sim "make -f simulator/gcc/Makefile -j\$(nproc)"
    ;;
  rust)
    # No container: cargo is on the host. The watch target is the check that
    # matters -- it is what proves the crates are no_std and that nothing
    # testable crept in behind feature = "std".
    for crate in "$REPO/EffortKit" "$REPO/Squash/Software/Libs/rust"; do
        ( cd "$crate" \
          && cargo test --features std \
          && cargo clippy --features std --all-targets -- -D warnings \
          && cargo build --release --target thumbv8m.main-none-eabihf )
    done
    ;;
  *)
    sed -n '3,30p' "${BASH_SOURCE[0]}" >&2
    exit 1
    ;;
esac
