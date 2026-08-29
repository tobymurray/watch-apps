#ifndef QRWIDGET_HPP
#define QRWIDGET_HPP

#include <touchgfx/widgets/Widget.hpp>

#include "Barcode.hpp"
#include "Matrix.hpp"

/**
 * @brief Draws a matrix barcode: a caller-supplied id rendered as a module grid.
 *
 * The counterpart to BarcodeWidget, and deliberately not the same widget. A
 * linear symbology is a run of widths scaled to a width; a matrix symbology is
 * a grid of squares at a fixed module size, and folding the two together would
 * put a branch on the path that draws every parkrun barcode to serve a format
 * that path cannot draw. See Docs/QR.md.
 *
 * ## Why this is not a CanvasWidget, and not a bitmap
 *
 * **Not a bitmap.** The 23x35 ABGR2222 button indicators in this app blit
 * corrupt on device -- a smear of horizontal dashes -- which is why it has no
 * on-screen button hints; see the comment in MainView::showBarcode(). Filled
 * rectangles are the path known to render correctly here, which is why the
 * pager marks are touchgfx::Box fills.
 *
 * **Not a CanvasWidget.** BarcodeWidget gets away with being one because a
 * Code 128 symbol is at most 115 quadrilaterals in a single horizontal sweep.
 * A 25x25 grid is up to 13 dark runs a row over 25 rows -- some 325 disjoint
 * rectangles against a CANVAS_BUFFER_SIZE of 3600 bytes. So this fills runs
 * directly through HAL::lcd(), exactly as touchgfx::Box does.
 *
 * The module size is whole pixels (BarcodeLayout::kQrModulePx), so no edge is
 * ever anti-aliased and the panel's four grey levels never come into it.
 *
 * The widget's own rect includes the quiet zone: it paints itself light and
 * then insets the dark modules, so the light margin *is* the widget, the same
 * way the white backing behind the bars is their quiet zone.
 */
class QrWidget : public touchgfx::Widget
{
public:
    QrWidget();

    /**
     * @brief Encode and display a new id.
     * @param format Which symbology to draw it as; must be a matrix one.
     * @param text   The id, as the format accepts it.
     * @retval true  Encoded; the widget has been invalidated.
     * @retval false Rejected by the encoder; the widget is left unchanged.
     */
    bool setCode(Barcode::Format format, const char *text);

    virtual void draw(const touchgfx::Rect &invalidatedArea) const;

    /// Opaque everywhere, so TouchGFX can skip whatever is behind it.
    virtual touchgfx::Rect getSolidRect() const;

protected:
    Barcode::Matrix matrix;
};

#endif // QRWIDGET_HPP
