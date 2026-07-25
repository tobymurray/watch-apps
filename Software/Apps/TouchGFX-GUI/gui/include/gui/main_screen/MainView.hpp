#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/containers/BarcodeWidget.hpp>
#include <touchgfx/widgets/Box.hpp>

#include "Barcode.hpp"

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /**
     * @brief Take in a new id from the service.
     */
    void onBarcodeChanged(const Barcode::State &state);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// White quiet zone the bars are drawn on top of, like the paper a real
    /// barcode is printed on -- needed since the screen background is black.
    touchgfx::Box barcodeBackground;
    BarcodeWidget barcode;

    /// Backing storage for textArea1's wildcard; the widget only stores the
    /// pointer, so this has to outlive it.
    touchgfx::Unicode::UnicodeChar idBuffer[Barcode::kMaxIdLength + 1];
};

#endif // MAINVIEW_HPP
