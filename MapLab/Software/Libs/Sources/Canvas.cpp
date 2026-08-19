#include "Canvas.hpp"

namespace MapLab
{
namespace
{

inline int16_t clampLow(int16_t v, int16_t lo)  { return v < lo ? lo : v; }
inline int16_t clampHigh(int16_t v, int16_t hi) { return v > hi ? hi : v; }

} // namespace

void Canvas::clear(uint8_t code)
{
    const uint32_t n = byteCount();
    for (uint32_t i = 0; i < n; ++i) {
        mPx[i] = code;
    }
}

void Canvas::plot(int16_t x, int16_t y, uint8_t code)
{
    if (x < 0 || y < 0 || x >= mW || y >= mH) {
        return;
    }
    mPx[static_cast<uint32_t>(y) * static_cast<uint32_t>(mW) + static_cast<uint32_t>(x)] = code;
}

void Canvas::hspan(int16_t y, int16_t x0, int16_t x1, uint8_t code)
{
    if (y < 0 || y >= mH) {
        return;
    }
    x0 = clampLow(x0, 0);
    x1 = clampHigh(x1, static_cast<int16_t>(mW - 1));
    if (x1 < x0) {
        return;
    }
    uint8_t* row = mPx + static_cast<uint32_t>(y) * static_cast<uint32_t>(mW);
    for (int16_t x = x0; x <= x1; ++x) {
        row[x] = code;
    }
}

void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t code)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const int16_t y0 = clampLow(y, 0);
    const int16_t y1 = clampHigh(static_cast<int16_t>(y + h - 1), static_cast<int16_t>(mH - 1));
    for (int16_t yy = y0; yy <= y1; ++yy) {
        hspan(yy, x, static_cast<int16_t>(x + w - 1), code);
    }
}

void Canvas::thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       int16_t width, uint8_t code)
{
    dashedLine(x0, y0, x1, y1, width, 1, 0, code);
}

void Canvas::dashedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                        int16_t width, int16_t onPx, int16_t offPx, uint8_t code)
{
    if (width < 1) {
        width = 1;
    }
    if (onPx < 1) {
        onPx = 1;
    }
    if (offPx < 0) {
        offPx = 0;
    }
    const int16_t half   = static_cast<int16_t>((width - 1) / 2);
    const int32_t dx     =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    const int32_t dy     = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    const int16_t stepX  = (x0 < x1) ? 1 : -1;
    const int16_t stepY  = (y0 < y1) ? 1 : -1;
    int32_t err          = dx + dy;
    const int16_t cycle  = static_cast<int16_t>(onPx + offPx);
    int16_t phase        = 0;

    for (;;) {
        if (phase < onPx) {
            if (width == 1) {
                plot(x0, y0, code);
            } else {
                fillRect(static_cast<int16_t>(x0 - half), static_cast<int16_t>(y0 - half),
                         width, width, code);
            }
        }
        if (++phase >= cycle) {
            phase = 0;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 = static_cast<int16_t>(x0 + stepX);
        }
        if (e2 <= dx) {
            err += dx;
            y0 = static_cast<int16_t>(y0 + stepY);
        }
    }
}

void Canvas::polyline(const Pt* pts, int count, int16_t width, uint8_t code)
{
    dashedPolyline(pts, count, width, 1, 0, code);
}

void Canvas::dashedPolyline(const Pt* pts, int count, int16_t width,
                            int16_t onPx, int16_t offPx, uint8_t code)
{
    if (pts == nullptr || count < 2) {
        // A one-point line is a dot, and a map that drops isolated nodes is
        // drawing less than it was given. Draw it.
        if (pts != nullptr && count == 1) {
            const int16_t half = static_cast<int16_t>(((width < 1 ? 1 : width) - 1) / 2);
            fillRect(static_cast<int16_t>(pts[0].x - half),
                     static_cast<int16_t>(pts[0].y - half),
                     width < 1 ? 1 : width, width < 1 ? 1 : width, code);
        }
        return;
    }
    for (int i = 1; i < count; ++i) {
        dashedLine(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y,
                   width, onPx, offPx, code);
    }
}

bool Canvas::fillPolygon(const Pt* pts, int count, uint8_t code)
{
    if (pts == nullptr || count < 3) {
        return true; // nothing to fill; not a failure
    }

    int16_t minY = pts[0].y;
    int16_t maxY = pts[0].y;
    for (int i = 1; i < count; ++i) {
        if (pts[i].y < minY) { minY = pts[i].y; }
        if (pts[i].y > maxY) { maxY = pts[i].y; }
    }
    minY = clampLow(minY, 0);
    maxY = clampHigh(maxY, static_cast<int16_t>(mH - 1));

    bool ok = true;

    for (int16_t y = minY; y <= maxY; ++y) {
        int32_t xs[kMaxCrossings];
        int     n = 0;

        // Half-open in y (`>= top && < bottom`) so a vertex shared by two
        // edges contributes exactly one crossing and horizontal edges
        // contribute none. Getting this wrong is what produces the classic
        // one-pixel bleed at a polygon's flat top.
        for (int i = 0, j = count - 1; i < count; j = i++) {
            const int32_t yi = pts[i].y;
            const int32_t yj = pts[j].y;
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                const int32_t xi = pts[i].x;
                const int32_t xj = pts[j].x;
                // Rounded rather than truncated: truncation biases every span
                // left by up to a pixel, which on a 2 px road is visible.
                const int32_t num = (xj - xi) * (y - yi);
                const int32_t den = (yj - yi);
                const int32_t x   = xi + (num + (den > 0 ? den / 2 : -den / 2)) / den;
                if (n < kMaxCrossings) {
                    xs[n++] = x;
                } else {
                    ++mDropped;
                    ok = false;
                }
            }
        }
        if (n < 2) {
            continue;
        }
        // Insertion sort: n is small and bounded, and this needs no scratch.
        for (int i = 1; i < n; ++i) {
            const int32_t v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                --j;
            }
            xs[j + 1] = v;
        }
        // Half-open in x as well as in y: fill [xa, xb), so two polygons
        // sharing an edge paint that column once between them rather than
        // twice, and a 100-wide box is 100 px wide rather than 101. Pinned by
        // Canvas_test's exact pixel counts, because an off-by-one here is
        // invisible on a screenshot and shows up as a seam only where two
        // fills of different colours meet.
        for (int i = 0; i + 1 < n; i += 2) {
            hspan(y, static_cast<int16_t>(xs[i]), static_cast<int16_t>(xs[i + 1] - 1), code);
        }
    }

    return ok;
}

void Canvas::applyLut(const uint8_t lut[kLutEntries])
{
    const uint32_t n = byteCount();
    for (uint32_t i = 0; i < n; ++i) {
        mPx[i] = lut[lutIndex(mPx[i])];
    }
}

} // namespace MapLab
