#ifndef BARCODEWIDGET_HPP
#define BARCODEWIDGET_HPP

#include <touchgfx/widgets/canvas/PainterABGR2222.hpp>
#include <touchgfx/widgets/canvas/Canvas.hpp>
#include <touchgfx/widgets/canvas/CanvasWidget.hpp>

#include "Code128.hpp"

/**
 * @brief Draws a Code 128 barcode: a caller-supplied id rendered as bars.
 *
 * Fills its own rect with alternating bar/space modules, scaled to the
 * widget's width, so the caller only has to position and size it like any
 * other widget.
 */
class BarcodeWidget : public touchgfx::CanvasWidget
{
public:
    BarcodeWidget();

    /**
     * @brief Encode and display a new id.
     * @param text Printable ASCII, at most Code128::kMaxDataLength characters.
     * @retval true  Encoded; the widget has been invalidated.
     * @retval false Rejected by the encoder; the widget is left unchanged.
     */
    bool setCode(const char *text);

    void setColor(touchgfx::colortype color);

    virtual touchgfx::Rect getMinimalRect() const;

    virtual bool drawCanvasWidget(const touchgfx::Rect &invalidatedArea) const;

protected:
    touchgfx::PainterABGR2222 painter;
    Code128::Encoded encoded;
};

#endif // BARCODEWIDGET_HPP
