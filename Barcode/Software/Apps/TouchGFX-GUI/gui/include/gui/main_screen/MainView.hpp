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
     * @brief Take in new codes -- or a new reason there aren't any.
     */
    void onBarcodeChanged(const Barcode::State &state);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// White quiet zone the bars are drawn on top of, like the paper a real
    /// barcode is printed on -- needed since the screen background is black.
    touchgfx::Box barcodeBackground;
    BarcodeWidget barcode;

    /// Every usable code, and which one is on screen. Held rather than
    /// re-requested because cycling must not wait on the service: the codes
    /// are already here, so L1/L2 only change which one is drawn.
    Barcode::State mState;
    uint8_t        mIndex;

    /// Backing storage for textArea1's wildcard; the widget only stores the
    /// pointer, so this has to outlive it.
    touchgfx::Unicode::UnicodeChar idBuffer[Barcode::kMaxIdLength + 1];

    /// The line above the bars: this code's name, alone. Position lives in the
    /// pager marks instead, so nothing is appended here.
    static const uint16_t kCaptionChars = Barcode::kMaxNameLength + 1;
    touchgfx::TextAreaWithOneWildcard caption;
    touchgfx::Unicode::UnicodeChar    captionBuffer[kCaptionChars];

    /// The id's second line, used only when even the 18pt face cannot fit the id
    /// on one -- twelve 'W's, say. textArea1 is the first line; see kIdLine1Y in
    /// MainView.cpp for why a wide id is split rather than cut.
    touchgfx::TextAreaWithOneWildcard idLine2;
    touchgfx::Unicode::UnicodeChar    idLine2Buffer[Barcode::kMaxIdLength + 1];

    /// Which of the codes is on screen, as one small mark each below the id --
    /// the current one white, the rest dim. Boxes rather than bitmaps: filled
    /// rectangles go through a path that renders correctly on device, unlike
    /// the button indicators (see showBarcode()).
    touchgfx::Box pagerMark[Barcode::kMaxCodes];

    /// The prompt is one text area per line rather than one wrapped area: a
    /// newline inside a wildcard is not a line break to TouchGFX, it is where
    /// the text stops, so a multi-line prompt in one wildcard renders as its
    /// first line and nothing else.
    static const uint16_t kPromptLines = 4;
    static const uint16_t kPromptChars = 24;
    touchgfx::TextAreaWithOneWildcard promptLine[kPromptLines];
    touchgfx::Unicode::UnicodeChar    promptBuffer[kPromptLines][kPromptChars];

    /// Put @p id in textArea1 at the largest face that fits, splitting it over
    /// idLine2 when even the smallest will not. Returns true if it split.
    bool layOutId(const char *id);

    /// Lay out and colour the pager marks for the current count and index.
    /// Hidden entirely when there is only one code.
    void showPager();

    /// Bars, with the id in readable text beneath them.
    void showBarcode();

    /// What to do instead. There is no keyboard, so the screen has to name
    /// the file that fixes it.
    void showPrompt(Barcode::Problem problem);

    /// Move @p delta codes, wrapping. No-op with fewer than two codes.
    void cycle(int delta);
};

#endif // MAINVIEW_HPP
