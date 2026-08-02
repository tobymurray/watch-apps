#include <gui/containers/BarcodeWidget.hpp>

BarcodeWidget::BarcodeWidget()
    : encoded{}
{
    painter.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    setPainter(painter);
}

bool BarcodeWidget::setCode(const char *text)
{
    Code128::Encoded next{};
    if (!Code128::encode(text, next)) {
        return false;
    }

    encoded = next;
    invalidate();
    return true;
}

void BarcodeWidget::setColor(touchgfx::colortype color)
{
    painter.setColor(color);
    invalidate();
}

touchgfx::Rect BarcodeWidget::getMinimalRect() const
{
    return touchgfx::Rect(0, 0, getWidth(), getHeight());
}

bool BarcodeWidget::drawCanvasWidget(const touchgfx::Rect &invalidatedArea) const
{
    if (encoded.totalModules == 0) {
        return true;
    }

    touchgfx::Canvas canvas(getPainter(), getAbsoluteRect(), invalidatedArea, getAlpha());

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
