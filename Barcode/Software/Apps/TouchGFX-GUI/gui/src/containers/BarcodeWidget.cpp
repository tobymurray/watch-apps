#include <gui/containers/BarcodeWidget.hpp>

#include "BarcodeLayout.hpp"
#include "Symbology.hpp"

BarcodeWidget::BarcodeWidget()
    : encoded{}
    , style(Barcode::Render::Scaled)
{
    painter.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    setPainter(painter);
}

bool BarcodeWidget::setCode(Barcode::Format format, const char *text)
{
    Barcode::Encoded next{};
    if (!Barcode::encode(format, text, next)) {
        return false;
    }

    encoded = next;
    style   = Barcode::renderStyle(format);
    invalidate();
    return true;
}

void BarcodeWidget::setColor(touchgfx::colortype color)
{
    painter.setColor(color);
    invalidate();
}

int16_t BarcodeWidget::barsHeight() const
{
    if (style != Barcode::Render::WholePixel || encoded.totalModules == 0) {
        return getHeight();
    }
    return BarcodeLayout::itfBarsHeightPx(BarcodeLayout::itfUnitPx(encoded.totalModules));
}

touchgfx::Rect BarcodeWidget::getMinimalRect() const
{
    return touchgfx::Rect(0, 0, getWidth(), getHeight());
}

void BarcodeWidget::fill(touchgfx::Canvas &canvas, int16_t x, int16_t y, int16_t w, int16_t h) const
{
    canvas.moveTo(x, y);
    canvas.lineTo(static_cast<int16_t>(x + w), y);
    canvas.lineTo(static_cast<int16_t>(x + w), static_cast<int16_t>(y + h));
    canvas.lineTo(x, static_cast<int16_t>(y + h));
}

bool BarcodeWidget::drawCanvasWidget(const touchgfx::Rect &invalidatedArea) const
{
    if (encoded.totalModules == 0) {
        return true;
    }

    touchgfx::Canvas canvas(getPainter(), getAbsoluteRect(), invalidatedArea, getAlpha());

    return style == Barcode::Render::WholePixel ? drawWholePixel(canvas) : drawScaled(canvas);
}

/**
 * Code 128, exactly as it has always been drawn: the run stretched to fill the
 * widget, in floating point, so an element is a fractional number of pixels and
 * its edges are anti-aliased. Left alone deliberately -- this is the path every
 * parkrun barcode goes through, and there is no scanner here to prove a change
 * to it safe.
 */
bool BarcodeWidget::drawScaled(touchgfx::Canvas &canvas) const
{
    const float moduleWidth = static_cast<float>(getWidth()) / static_cast<float>(encoded.totalModules);
    const float height = static_cast<float>(getHeight());

    // Modules alternate bar/space, always starting with a bar; only bars need
    // a shape, spaces are just the gap between them.
    float x = 0.0f;
    bool isBar = true;
    for (uint8_t i = 0; i < encoded.count; i++) {
        const float width = encoded.widths[i] * moduleWidth;
        if (isBar) {
            canvas.moveTo(x, 0.0f);
            canvas.lineTo(x + width, 0.0f);
            canvas.lineTo(x + width, height);
            canvas.lineTo(x, height);
        }
        x += width;
        isBar = !isBar;
    }

    return canvas.render();
}

/**
 * ITF: every element a whole number of pixels, the symbol centred in what that
 * leaves, and a bearer bar flush above and below it.
 *
 * Every coordinate below is an integer, which is the whole point -- on a panel
 * with four levels a channel a bar edge that lands mid-pixel steps rather than
 * blends, and the decoder's job is to compare element widths. Rounding the
 * element width down costs some symbol width; the quiet zone takes it, and it
 * was the tighter constraint anyway. BarcodeLayout has the arithmetic and the
 * tests hold it to it.
 */
bool BarcodeWidget::drawWholePixel(touchgfx::Canvas &canvas) const
{
    const int16_t unitPx = BarcodeLayout::itfUnitPx(encoded.totalModules);
    const int16_t bearer = BarcodeLayout::itfBearerPx(unitPx);
    const int16_t barsH  = BarcodeLayout::itfBarsHeightPx(unitPx);

    // Centred in the widget rather than at its left edge: the widget spans the
    // whole band, and the symbol is narrower than that by however much the
    // rounding gave back.
    const int16_t width = static_cast<int16_t>(encoded.totalModules * unitPx);
    const int16_t left  = static_cast<int16_t>((getWidth() - width) / 2);

    // The bearers span the symbol *and* its quiet zone, so a scan clipping the
    // top or bottom crosses ink wherever it enters. Full widget width is that,
    // since the widget is the band and the band is the quiet zone.
    fill(canvas, 0, 0, getWidth(), bearer);
    fill(canvas, 0, static_cast<int16_t>(bearer + barsH), getWidth(), bearer);

    int16_t x = left;
    bool isBar = true;
    for (uint8_t i = 0; i < encoded.count; i++) {
        const int16_t w = static_cast<int16_t>(encoded.widths[i] * unitPx);
        if (isBar) {
            fill(canvas, x, bearer, w, barsH);
        }
        x = static_cast<int16_t>(x + w);
        isBar = !isBar;
    }

    return canvas.render();
}
