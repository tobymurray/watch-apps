#include "SceneRender.hpp"

namespace MapLab
{

StrokeSpec strokeFor(Slot klass)
{
    switch (klass) {
        // major road: 4 px ink, 7 px halo  (=> 1.5 px of halo each side)
        case Slot::RoadMajor: return { 4, 7, 0, 0 };
        // minor road: 2 px ink, 5 px halo
        case Slot::RoadMinor: return { 2, 5, 0, 0 };
        // path: 2 px, dashed 3 on / 3 off, no casing -- the dash is the trail
        // signifier and the hue is the confirmation
        case Slot::Path:      return { 2, 0, 3, 3 };
        case Slot::WaterDark: return { 2, 0, 0, 0 };
        // contour: 1 px, and 1 px is the floor. It works here because a
        // contour is a texture read collectively, not a feature read singly.
        case Slot::Contour:   return { 1, 0, 0, 0 };
        default:              return { 1, 0, 0, 0 };
    }
}

RenderStats renderScene(const SceneReader& scene, Canvas& canvas,
                        Pt* scratch, int scratchCap,
                        int16_t originX, int16_t originY, int16_t tilePx)
{
    RenderStats st;
    const uint32_t droppedBefore = canvas.dropped();

    for (int i = 0; i < scene.layerCount(); ++i) {
        const LayerInfo& L = scene.layer(i);
        if (L.featureCount == 0) {
            continue; // empty, or a class this reader does not know
        }
        const Slot  klass = static_cast<Slot>(L.klass);
        const uint8_t ink = code(klass);

        if (L.kind == Kind::Polygon) {
            const int n = scene.forEachFeature(
                i, scratch, scratchCap, originX, originY, tilePx,
                [&](const Pt* pts, int count) {
                    st.points += static_cast<uint32_t>(count);
                    ++st.polygons;
                    canvas.fillPolygon(pts, count, ink);
                    // Water gets a 1 px darker edge (§ 4). Everything else is
                    // a flat fill: an ambient wash with an outline would spend
                    // ink the palette has already committed elsewhere.
                    if (klass == Slot::Water) {
                        canvas.polyline(pts, count, 1, code(Slot::WaterDark));
                        if (count > 1) {
                            canvas.thickLine(pts[count - 1].x, pts[count - 1].y,
                                             pts[0].x, pts[0].y, 1, code(Slot::WaterDark));
                        }
                    }
                });
            if (n > 0) {
                st.features += static_cast<uint32_t>(n);
            }
            continue;
        }

        const StrokeSpec s = strokeFor(klass);

        // Casing first, for every feature in the layer, before any ink in it.
        if (s.casing > 0) {
            scene.forEachFeature(i, scratch, scratchCap, originX, originY, tilePx,
                                 [&](const Pt* pts, int count) {
                                     st.points += static_cast<uint32_t>(count);
                                     canvas.polyline(pts, count, s.casing, kHalo);
                                 });
        }

        const int n = scene.forEachFeature(
            i, scratch, scratchCap, originX, originY, tilePx,
            [&](const Pt* pts, int count) {
                st.points += static_cast<uint32_t>(count);
                ++st.polylines;
                if (count >= scratchCap) {
                    ++st.clipped;
                }
                if (s.dashOn > 0) {
                    canvas.dashedPolyline(pts, count, s.width, s.dashOn, s.dashOff, ink);
                } else {
                    canvas.polyline(pts, count, s.width, ink);
                }
            });
        if (n > 0) {
            st.features += static_cast<uint32_t>(n);
        }
    }

    st.droppedSpans = canvas.dropped() - droppedBefore;
    return st;
}

} // namespace MapLab
