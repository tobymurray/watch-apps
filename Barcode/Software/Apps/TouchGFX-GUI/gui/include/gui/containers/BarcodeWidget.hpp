#ifndef BARCODEWIDGET_HPP
#define BARCODEWIDGET_HPP

#include <touchgfx/widgets/canvas/PainterABGR2222.hpp>
#include <touchgfx/widgets/canvas/Canvas.hpp>
#include <touchgfx/widgets/canvas/CanvasWidget.hpp>

#include "Barcode.hpp"
#include "Encoded.hpp"
#include "Symbology.hpp"

/**
 * @brief Draws a linear barcode: a caller-supplied id rendered as bars.
 *
 * Fills its own rect with alternating bar/space elements, so the caller only
 * has to position and size it like any other widget.
 *
 * It does not know which symbology it is drawing and has no reason to: a run
 * of alternating widths is the whole of what any linear symbology produces,
 * and Barcode::Encoded carries nothing else. setCode() names a format only to
 * pass it on, and to ask Barcode::renderStyle() which of two layouts to use --
 * so the answer lives in Symbology.hpp with the rest of the format knowledge
 * and a third linear format needs no change here.
 *
 * The two layouts:
 *
 *   Scaled      elements stretched to fill the widget, edges anti-aliased.
 *               Code 128, unchanged.
 *   WholePixel  whole-pixel elements, symbol centred, plus a bearer bar above
 *               and below. ITF. Nothing is anti-aliased, and the bearers are
 *               what stop a clipped scan decoding as a shorter number.
 */
class BarcodeWidget : public touchgfx::CanvasWidget
{
public:
    BarcodeWidget();

    /**
     * @brief Encode and display a new id.
     * @param format Which symbology to draw it as.
     * @param text   The id, as the format accepts it.
     * @retval true  Encoded; the widget has been invalidated.
     * @retval false Rejected by the encoder; the widget is left unchanged.
     */
    bool setCode(Barcode::Format format, const char *text);

    void setColor(touchgfx::colortype color);

    /// Height the bars actually occupy, which is less than the widget's when
    /// bearer bars are taken out of it. For the caller that positions the id.
    int16_t barsHeight() const;

    virtual touchgfx::Rect getMinimalRect() const;

    virtual bool drawCanvasWidget(const touchgfx::Rect &invalidatedArea) const;

protected:
    /// Draw one filled rectangle in widget coordinates.
    void fill(touchgfx::Canvas &canvas, int16_t x, int16_t y, int16_t w, int16_t h) const;

    bool drawScaled(touchgfx::Canvas &canvas) const;
    bool drawWholePixel(touchgfx::Canvas &canvas) const;

    touchgfx::PainterABGR2222 painter;
    Barcode::Encoded encoded;
    Barcode::Render  style;
};

#endif // BARCODEWIDGET_HPP
