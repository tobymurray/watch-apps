#!/usr/bin/env bash
#
# Build HapticLab, or its host tests, or its renderer, in containers where
# that is what CI uses, so the result does not depend on what happens to be
# installed on this machine.
#
# **All three have to pass before a branch is believed.** They disagree about
# what exists:
#
#   * the ARM build is the only one that links the watch's newlib, where
#     floating-point `printf` may be absent and, when it is, `%f` emits nothing
#     at runtime rather than failing at link time. Nothing this app writes is
#     fractional, and no build can check that for you;
#   * the host tests are the only ones that see the SDK's JSON writer turn a
#     negative `int32_t` into its unsigned reinterpretation, which is why the
#     rows are CSV written by hand;
#   * the renderer's tests are the only ones that see the round panel, and the
#     `preview` binary is the only way to look at a screen without a watch.
#
# There is no TouchGFX simulator here: the GUI is a Rust renderer behind
# CustomGUI, so `preview` is what a simulator would have been, and it masks the
# bezel, which the SDK's simulator does not.
#
# Usage:
#   Tools/docker-build.sh app        # the .uapp
#   Tools/docker-build.sh tests      # host tests: configure, build, ctest
#   Tools/docker-build.sh renderer   # the renderer's own tests, on this machine
#   Tools/docker-build.sh preview     # one disc-masked PNG per screen
#   Tools/docker-build.sh fixture    # regenerate the analysis fixture
#   Tools/docker-build.sh analyse <run-dir>...   # the report
#   Tools/docker-build.sh all        # app, tests and renderer
#
# The image is the one this repository's own CI pins by digest
# (.github/workflows/app-build.yml). Override with $HAPTICLAB_IMAGE.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$APP/.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
APP_VERSION="${BUILD_VERSION:-0.1.0}"

IMAGE="${HAPTICLAB_IMAGE:-ghcr.io/tobymurray/watch-apps-toolchain@sha256:cca44e2ca090850480a612f4ed3d4acbd317f3bd32516b0d8fe323cf37c0ee26}"

# The build directories live inside the repository rather than in /tmp, because
# each `docker run` is a fresh container and a /tmp build would not survive
# between the configure and the ctest. `build/` is gitignored at the root.
TESTS_BUILD="HapticLab/Tests/build"

run() {
    docker run --rm --platform linux/amd64 \
        -v "$REPO:/apps" -v "$SDK:/sdk" -e UNA_SDK=/sdk -w /apps \
        "$IMAGE" bash -lc "$1"
}

build_tests() {
    run "cmake -S HapticLab/Tests -B $TESTS_BUILD -DCMAKE_BUILD_TYPE=Debug \
         && cmake --build $TESTS_BUILD -j\$(nproc)"
}

case "${1:-}" in
  app)
    run "cd HapticLab/Software/Apps/HapticLab-CMake \
         && cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=$APP_VERSION . \
         && cmake --build build -j\$(nproc)"
    ;;
  tests)
    build_tests
    run "cd $TESTS_BUILD && ctest --output-on-failure"
    ;;
  renderer)
    # No container: cargo and the thumbv8m target are what a developer already
    # has, and the crate is the one part of this app with no SDK in it.
    (cd "$APP/Software/Apps/CustomGUI/rust" && cargo test --features std)
    ;;
  preview)
    (cd "$APP/Software/Apps/CustomGUI/rust" \
        && cargo run --features preview --bin preview -- "${2:-$APP/Docs/screens}")
    ;;
  fixture)
    python3 "$APP/Tests/fixtures/make_fixture.py" "$APP/Tests/fixtures"
    ;;
  analyse)
    shift
    [ $# -ge 1 ] || { echo "usage: docker-build.sh analyse <run-dir>..." >&2; exit 2; }
    build_tests
    dirs=""
    for dir in "$@"; do
        dirs="$dirs '/apps/${dir#"$REPO/"}'"
    done
    run "$TESTS_BUILD/hapticlab-analyse $dirs"
    ;;
  all)
    "$0" renderer
    "$0" tests
    "$0" app
    ;;
  *)
    sed -n '2,40p' "$0"
    exit 2
    ;;
esac
