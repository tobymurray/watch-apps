/**
 ******************************************************************************
 * @file    RestfulnessBand.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A relative index from movement and heart rate. NOT a sleep stage.
 *          Read the header before using the output anywhere.
 ******************************************************************************
 */

#include "Engine/RestfulnessBand.hpp"

#include "Engine/Epoch.hpp"

namespace Engine
{

constexpr char RestfulnessBand::kMethod[];
constexpr char RestfulnessBand::kCaption[];

namespace {

/// Movement's contribution, 0 (least settled) .. 2 (most).
int movementScore(uint32_t count)
{
    if (count <= RestfulnessBand::kMoveSettled)  { return 2; }
    if (count >= RestfulnessBand::kMoveRestless) { return 0; }
    return 1;
}

/// Heart rate's contribution, 0 .. 2, relative to the night's own minimum.
int hrScore(int16_t hrX10, int32_t nightMinX10)
{
    const int32_t above = static_cast<int32_t>(hrX10) - nightMinX10;
    if (above <= RestfulnessBand::kHrSettledX10)  { return 2; }
    if (above >= RestfulnessBand::kHrRestlessX10) { return 0; }
    return 1;
}

} // namespace

bool RestfulnessBand::compute(const ScoringInput *in, const Verdict *v, size_t n,
                              int32_t hrMinX10, Restfulness *out)
{
    if (in == nullptr || v == nullptr || out == nullptr) {
        return false;
    }

    const bool haveNightMin = (hrMinX10 != kAbsent);
    bool       usedHr       = false;

    for (size_t i = 0; i < n; ++i) {
        // Awake, or not enough evidence to have scored the epoch at all. A
        // restfulness value here would describe nothing, and drawing one would
        // fill the gaps in the strip with something that looks like data.
        if (v[i] != Verdict::Sleep) {
            out[i] = Restfulness::Unknown;
            continue;
        }

        const int move = movementScore(in[i].count);

        const bool epochHasHr = haveNightMin &&
                                in[i].hrMeanX10 != static_cast<int16_t>(kAbsent) &&
                                in[i].hrMeanX10 > 0;

        int total;
        if (epochHasHr) {
            usedHr = true;
            // Both channels, equally weighted, 0..4. Equal weighting is a
            // choice and not a finding: there is no evidence here for
            // preferring one channel over the other, and inventing a ratio
            // would be a precision this has not earned.
            total = move + hrScore(in[i].hrMeanX10, hrMinX10);
        } else {
            // Movement only. Doubled so the scale is the same 0..4 either way
            // -- otherwise an epoch that happened to lack a heart rate would
            // draw darker than its neighbours for no reason the wearer could
            // see. That the two paths are not equally informative is recorded
            // by the return value, not smuggled into the band's level.
            total = move * 2;
        }

        // 0..4 collapsed onto three named levels. Three rather than four
        // because Unknown occupies the fourth slot in the palette, and a band
        // that used all four for data would leave nothing to draw "no data"
        // in -- which is precisely the value that must stay visually distinct.
        if (total >= 4) {
            out[i] = Restfulness::Deepest;
        } else if (total >= 2) {
            out[i] = Restfulness::Settled;
        } else {
            out[i] = Restfulness::Restless;
        }
    }

    return usedHr;
}

} // namespace Engine
