#include "Palette.hpp"

namespace MapLab
{
namespace
{

void identity(uint8_t out[kLutEntries])
{
    for (int i = 0; i < kLutEntries; ++i) {
        out[i] = static_cast<uint8_t>(0xC0 | i); // opaque alpha, low six bits
    }
}

void set(uint8_t out[kLutEntries], uint8_t from, uint8_t to)
{
    out[lutIndex(from)] = to;
}

} // namespace

bool buildLut(Variant v, uint8_t out[kLutEntries])
{
    identity(out);

    switch (v) {
        case Variant::Day:
            // Identity, and that is the point: Day is what the pack authored.
            return true;

        case Variant::Night:
            // "Inverted maps need *one* dark tone for ground and many light
            // ones for features -- which is the shape this panel has"
            // (MAP_CARTOGRAPHY_SPEC.md § 9). Ground goes to the single neutral
            // dark code; the inks go to paper; the fills take the two
            // chromatic darks so water still reads as water.
            set(out, code(Slot::Paper),     code(Slot::RoadMajor));
            set(out, code(Slot::Landuse),   code(Slot::RoadMinor));
            set(out, code(Slot::WoodLight), code(Slot::RoadMinor));
            set(out, code(Slot::Building),  code(Slot::RoadMinor));
            set(out, code(Slot::Wood),      code(Slot::RoadMinor));
            set(out, code(Slot::Water),     code(Slot::Path));
            set(out, code(Slot::WaterDark), code(Slot::Path));
            set(out, code(Slot::RoadMajor), code(Slot::Paper));
            set(out, code(Slot::RoadMinor), code(Slot::Paper));
            set(out, code(Slot::Path),      code(Slot::Paper));
            set(out, code(Slot::Contour),   code(Slot::Building));
            // `trace` is untouched. It is drawn by the app over the blit and
            // must win against every basemap colour in every variant (R5).
            return true;

        case Variant::HighContrast:
            // Five codes: ink, paper, water, and the two the trace and its
            // background need. Everything ambient collapses into paper.
            set(out, code(Slot::Landuse),   code(Slot::Paper));
            set(out, code(Slot::WoodLight), code(Slot::Paper));
            set(out, code(Slot::Building),  code(Slot::Paper));
            set(out, code(Slot::Wood),      code(Slot::Paper));
            set(out, code(Slot::Contour),   code(Slot::Paper));
            set(out, code(Slot::RoadMinor), code(Slot::RoadMajor));
            set(out, code(Slot::Path),      code(Slot::RoadMajor));
            set(out, code(Slot::WaterDark), code(Slot::Water));
            return true;

        case Variant::Trail:
            // Demote what a runner on a trail does not need, promote what they
            // do. No pack byte changes; this is the whole argument for
            // one excellent pack plus four tables.
            set(out, code(Slot::Building),  code(Slot::Paper));
            set(out, code(Slot::Landuse),   code(Slot::Paper));
            set(out, code(Slot::RoadMinor), code(Slot::Building));
            set(out, code(Slot::Path),      code(Slot::RoadMajor));
            set(out, code(Slot::RoadMajor), code(Slot::RoadMinor));
            set(out, code(Slot::Contour),   code(Slot::RoadMinor));
            return true;

        default:
            return false;
    }
}

const char* variantName(Variant v)
{
    switch (v) {
        case Variant::Day:          return "day";
        case Variant::Night:        return "night";
        case Variant::HighContrast: return "contrast";
        case Variant::Trail:        return "trail";
        default:                    return "?";
    }
}

} // namespace MapLab
