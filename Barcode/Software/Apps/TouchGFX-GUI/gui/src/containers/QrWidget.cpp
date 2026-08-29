#include <gui/containers/QrWidget.hpp>

#include <touchgfx/Color.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>

#include "BarcodeLayout.hpp"
#include "Symbology.hpp"

namespace
{

/// Black on white, the way a printed barcode is, and not configurable: a
/// matrix symbology's decoder expects dark modules on a light quiet zone and
/// nothing about this app wants to negotiate that.
const touchgfx::colortype kDark  = touchgfx::Color::getColorFromRGB(0, 0, 0);
const touchgfx::colortype kLight = touchgfx::Color::getColorFromRGB(255, 255, 255);

/// Fully opaque, always. touchgfx::Widget carries no alpha of its own -- Box
/// adds one -- and this is a case where not having it is right: a translucent
/// barcode is a scanning defect rather than a style, and blending against the
/// black screen background is exactly what would destroy the contrast a
/// decoder needs.
constexpr uint8_t kOpaque = 255;

constexpr int16_t kModule = BarcodeLayout::kQrModulePx;
constexpr int16_t kInset  = BarcodeLayout::kQrQuietModules * BarcodeLayout::kQrModulePx;

} // namespace

QrWidget::QrWidget()
    : matrix{}
{
}

bool QrWidget::setCode(Barcode::Format format, const char *text)
{
    Barcode::Matrix next{};
    if (!Barcode::encode(format, text, next)) {
        return false;
    }

    matrix = next;
    invalidate();
    return true;
}

touchgfx::Rect QrWidget::getSolidRect() const
{
    return matrix.size == 0 ? touchgfx::Rect() : touchgfx::Rect(0, 0, getWidth(), getHeight());
}

void QrWidget::draw(const touchgfx::Rect &invalidatedArea) const
{
    if (matrix.size == 0) {
        return;
    }

    // The quiet zone, and the light half of every module: painted first as one
    // fill so the run loop below only has to draw the dark runs.
    touchgfx::Rect light = invalidatedArea;
    translateRectToAbsolute(light);
    touchgfx::HAL::lcd().fillRect(light, kLight, kOpaque);

    // One fill per run of adjacent dark modules in a row, rather than one per
    // module: a run is what a fill is good at, and 25 rows of a few runs each
    // is an order of magnitude fewer calls than 625 squares would be.
    for (uint8_t y = 0; y < matrix.size; y++) {
        uint8_t x = 0;
        while (x < matrix.size) {
            if (!matrix.dark(x, y)) {
                x++;
                continue;
            }

            uint8_t end = x;
            while (end < matrix.size && matrix.dark(end, y)) {
                end++;
            }

            touchgfx::Rect run(static_cast<int16_t>(kInset + x * kModule),
                               static_cast<int16_t>(kInset + y * kModule),
                               static_cast<int16_t>((end - x) * kModule),
                               kModule);
            run &= invalidatedArea;
            if (!run.isEmpty()) {
                translateRectToAbsolute(run);
                touchgfx::HAL::lcd().fillRect(run, kDark, kOpaque);
            }

            x = end;
        }
    }
}
