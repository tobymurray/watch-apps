#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/containers/BarcodeWidget.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

#include "Barcode.hpp"

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /**
     * @brief Take in a new id -- or a new reason there isn't one.
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

    /// The prompt is one text area per line rather than one wrapped area: a
    /// newline inside a wildcard is not a line break to TouchGFX, it is where
    /// the text stops, so a multi-line prompt in one wildcard renders as its
    /// first line and nothing else.
    static const uint16_t kPromptLines = 4;
    static const uint16_t kPromptChars = 24;
    touchgfx::TextAreaWithOneWildcard promptLine[kPromptLines];
    touchgfx::Unicode::UnicodeChar    promptBuffer[kPromptLines][kPromptChars];

    /// Bars, with the id in readable text beneath them.
    void showBarcode(const Barcode::State &state);

    /// What to do instead. There is no keyboard, so the screen has to name
    /// the file that fixes it.
    void showPrompt(Barcode::Problem problem);
};

#endif // MAINVIEW_HPP
