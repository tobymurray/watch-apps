/**
 ******************************************************************************
 * @file    SceneRender.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Draw a decoded tile the way MAP_CARTOGRAPHY_SPEC.md § 4 says to.
 ******************************************************************************
 *
 * The style is not a parameter. Line weights, casing and dash pattern come
 * from the spec's table, because the number this app exists to produce is
 * "what does the specified map cost to draw", not "what does some map cost to
 * draw". If the spec's weights change after somebody looks at the panel, this
 * table changes with it and the benchmark is re-run.
 *
 * ---------------------------------------------------------------------------
 * CASING COSTS A SECOND PASS, AND THAT IS MEASURED RATHER THAN AVOIDED
 *
 * A cased road is a wide `paper` line with a narrow ink line on top, and every
 * road's casing must be under every road's ink -- otherwise a junction shows
 * one road's halo cutting through another's surface. So the layer is decoded
 * twice: once for the casing pass, once for the ink pass.
 *
 * The alternative is decoding once into a buffer and drawing it twice, which
 * costs RAM this budget does not have (a city-centre road layer is thousands
 * of points). Decoding twice is the arrangement a shipping renderer would
 * actually use here, so it is the arrangement that gets timed. `RenderStats`
 * reports both passes' points so the report can say what the second pass cost.
 *
 * Pure: no SDK, no TouchGFX. Host-tested.
 ******************************************************************************
 */

#ifndef MAPLAB_SCENERENDER_HPP
#define MAPLAB_SCENERENDER_HPP

#include "Canvas.hpp"
#include "VecScene.hpp"

namespace MapLab
{

/// What one render actually did. Every bench that renders reports these
/// beside its timing: a render that drew fewer points because a scratch
/// buffer clipped a feature is a faster number and a worse map, and the two
/// must not be confusable.
struct RenderStats {
    uint32_t features   = 0;
    uint32_t points     = 0;  ///< Points decoded, both passes counted.
    uint32_t polygons   = 0;
    uint32_t polylines  = 0;
    uint32_t clipped    = 0;  ///< Features longer than the scratch buffer.
    uint32_t droppedSpans = 0;///< Canvas::dropped() delta -- see Canvas.hpp.
};

/// Stroke widths and dash, per class, from MAP_CARTOGRAPHY_SPEC.md § 4.
struct StrokeSpec {
    int16_t width;      ///< Ink stroke.
    int16_t casing;     ///< Paper halo width; 0 for none.
    int16_t dashOn;     ///< 0 = solid.
    int16_t dashOff;
};

/// The spec's table, addressable by class.
StrokeSpec strokeFor(Slot klass);

/**
 * @brief Render one tile into the canvas.
 *
 * @param origin{X,Y}  where the tile's (0,0) lands on the canvas
 * @param tilePx       the tile's on-screen size in pixels; this is what zoom
 *                     means to the renderer, and drawing a tile at more pixels
 *                     than it was generalised for is exactly the overzoom case
 *                     card C6 asks the eye about.
 * @param scratch      caller-owned point buffer; never allocated here, and
 *                     sized by the caller so its cost shows up in the app's
 *                     static footprint rather than on the stack.
 */
RenderStats renderScene(const SceneReader& scene, Canvas& canvas,
                        Pt* scratch, int scratchCap,
                        int16_t originX, int16_t originY, int16_t tilePx);

} // namespace MapLab

#endif // MAPLAB_SCENERENDER_HPP
