#!/usr/bin/env bash
#
# Gate B: does a vector renderer's static working set fit in RunMap's GUI?
#
# MapLab cannot answer this. Its own GUI is a stopwatch shell with room to
# spare; the ceiling that matters belongs to RunMap, the largest of the three
# map GUIs and the one that pinned `TileCache::SLOTS = 1` -- 2 slots overflowed
# `.bss` by 33,884 bytes against a pristine apps-v1.3.0, 4 slots by 165,044.
#
# The instrument is that same constant, and it is the instrument *because* it
# is real code. The obvious alternative -- drop a file holding a big static
# array into `gui/src/` -- was tried first and silently does nothing: the array
# never reaches the link, `.bss` does not move, and the test reports "links" for
# every size you give it. `__attribute__((used))`, `retain` and an explicit
# `section(".bss")` all failed to save it. A measurement instrument that always
# says yes is worse than none, so this one drives a buffer the app genuinely
# allocates and reads.
#
#   Tools/gate_b_link_test.sh --slots 2       # reproduce the historical figure
#   Tools/gate_b_link_test.sh --bytes 84224   # can a renderer's set fit?
#   Tools/gate_b_link_test.sh --bisect        # the largest that links
#
# `--bytes N` resizes the cache's per-slot buffer to N with one slot, which
# answers the question that matters: a vector renderer *replaces* the tile
# cache, so what it may spend is N, not N on top of 64 KiB. The candidate set
# is 84,224 B -- a 240x240 ABGR2222 canvas (57,600), a 24 KiB encoded tile and
# a 2 KiB decoder scratch.
#
# CAVEAT: RunMap is pinned to apps-v1.3.0 and the watch now runs the 1.4 line,
# so point UNA_SDK at whichever SDK the map apps will ship against and record
# which one you used. The two do not have to agree.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${UNA_SDK:-$(cd "$REPO/../una-sdk" && pwd)}"
IMAGE="${MAPLAB_ARM_IMAGE:-sleeplab-arm:latest}"
APP="${GATE_B_APP:-RunMap}"

HEADER="$REPO/MapKit/Header/MapKit/TileCache.hpp"
BACKUP="$(mktemp)"
cp "$HEADER" "$BACKUP"

cleanup() {
    cp "$BACKUP" "$HEADER"
    rm -f "$BACKUP"
    rm -rf "$REPO/$APP/Software/Apps/$APP-CMake/build-gateb"
}
trap cleanup EXIT

patch_slots() {
    sed -i.tmp "s/static constexpr uint32_t SLOTS      = [0-9]*;/static constexpr uint32_t SLOTS      = $1;/" "$HEADER"
    rm -f "$HEADER.tmp"
}

patch_bytes() {
    sed -i.tmp "s#static constexpr uint32_t TILE_BYTES = .*;#static constexpr uint32_t TILE_BYTES = $1;#" "$HEADER"
    rm -f "$HEADER.tmp"
}

build() {
    docker run --rm --platform linux/amd64 \
        -v "$REPO:/w" -v "$SDK:/sdk" -e UNA_SDK=/sdk \
        -w "/w/$APP/Software/Apps/$APP-CMake" \
        "$IMAGE" bash -lc \
        "rm -rf build-gateb && cmake -B build-gateb -G 'Unix Makefiles' -DBUILD_VERSION=0.0.0 . >/dev/null 2>&1 \
         && cmake --build build-gateb -j\$(nproc) 2>&1" 2>&1
}

report() {
    local label="$1" log="$2" ok="$3"
    if [[ "$ok" == "yes" ]]; then
        local bss
        bss="$(grep -oE '\.bss +[0-9]+' <<<"$log" | head -1 || true)"
        echo "  ${label}: links${bss:+   ($bss)}"
        return 0
    fi
    local overflow
    overflow="$(grep -oE "region \`RAM' overflowed by [0-9]+ bytes" <<<"$log" | head -1 || true)"
    if [[ -n "$overflow" ]]; then
        echo "  ${label}: DOES NOT FIT -- ${overflow#region \`RAM\' }"
    else
        echo "  ${label}: build failed for another reason:"
        grep -iE "error" <<<"$log" | head -4 | sed 's/^/      /'
    fi
    return 1
}

try() {
    local label="$1"
    local log ok=yes
    log="$(build)" || ok=no
    log+=$'\n'"$(docker run --rm --platform linux/amd64 -v "$REPO:/w" "$IMAGE" \
        bash -lc "arm-none-eabi-size -A /w/$APP/Software/Apps/$APP-CMake/build-gateb/${APP}GUI.elf 2>/dev/null | grep '\.bss'" || true)"
    report "$label" "$log" "$ok"
}

echo "Gate B against $APP, SDK $SDK"
echo

case "${1:-}" in
    --slots)
        patch_slots "${2:?usage: --slots N}"
        try "TileCache::SLOTS = $2"
        ;;
    --bisect)
        lo=0
        hi=$((192 * 1024))
        patch_bytes 4096
        if ! try "4 KiB baseline"; then
            echo "the baseline does not build -- fix that before reading anything below"
            exit 1
        fi
        lo=4096
        while (( hi - lo > 4096 )); do
            mid=$(((lo + hi) / 2))
            patch_bytes "$mid"
            if try "$((mid / 1024)) KiB"; then lo=$mid; else hi=$mid; fi
        done
        echo
        echo "largest single static buffer that links in $APP's GUI: ~$((lo / 1024)) KiB"
        echo "(that is in place of the tile cache, not on top of it)"
        ;;
    --bytes|"")
        bytes="${2:-84224}"
        patch_bytes "$bytes"
        try "$bytes B in place of the tile cache" || true
        ;;
    *)
        sed -n '2,32p' "$0"
        exit 2
        ;;
esac
