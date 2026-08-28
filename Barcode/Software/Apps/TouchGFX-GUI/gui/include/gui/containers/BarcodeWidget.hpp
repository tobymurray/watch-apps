#ifndef BARCODEWIDGET_HPP
#define BARCODEWIDGET_HPP

#include <touchgfx/widgets/canvas/PainterABGR2222.hpp>
#include <touchgfx/widgets/canvas/Canvas.hpp>
#include <touchgfx/widgets/canvas/CanvasWidget.hpp>

#include "Barcode.hpp"
#include "Encoded.hpp"

/**
 * @brief Draws a linear barcode: a caller-supplied id rendered as bars.
 *
 * Fills its own rect with alternating bar/space elements, scaled to the
 * widget's width, so the caller only has to position and size it like any
 * other widget.
 *
 * It does not know which symbology it is drawing and has no reason to: a run
 * of alternating widths is the whole of what any linear symbology produces,
 * and Barcode::Encoded carries nothing else. Only setCode() names a format,
 * and only to pass it on.
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

    virtual touchgfx::Rect getMinimalRect() const;

    virtual bool drawCanvasWidget(const touchgfx::Rect &invalidatedArea) const;

protected:
    touchgfx::PainterABGR2222 painter;
    Barcode::Encoded encoded;
};

#endif // BARCODEWIDGET_HPP
