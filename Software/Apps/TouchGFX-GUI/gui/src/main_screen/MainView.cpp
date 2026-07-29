#include <gui/main_screen/MainView.hpp>

MainView::MainView()
    : idBuffer{}
{
    barcodeBackground.setPosition(10, 65, 220, 110);
    barcodeBackground.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(barcodeBackground);

    barcode.setPosition(20, 75, 200, 90);
    add(barcode);
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);
    buttons.setR1(ButtonsSet::NONE);
    buttons.setR2(ButtonsSet::NONE);

    textArea1.setWildcard1(idBuffer);

    onBarcodeChanged(presenter->barcode());
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}

void MainView::onBarcodeChanged(const Barcode::State &state)
{
    barcode.setCode(state.id);

    touchgfx::Unicode::strncpy(idBuffer, state.id, Barcode::kMaxIdLength + 1);
    textArea1.invalidate();
}
