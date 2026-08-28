#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>

namespace
{

/// Four 18pt lines, centred on the face and inset far enough to clear the
/// corners the round display cuts off.
constexpr int16_t kPromptX = 20, kPromptW = 200, kPromptLineH = 24, kPromptTop = 72;

/// The caption sits in the band above the bars, inset past the button ticks
/// the bezel container draws down either side.
constexpr int16_t kCaptionX = 40, kCaptionY = 48, kCaptionW = 160, kCaptionH = 24;

/// One mark per code, in the band the arc leaves below the id: kMarkW wide on
/// a kMarkPitch grid, centred on the face. Six of them span 83px of the ~130
/// the circle still allows at kMarkY, and Commands.hpp caps kMaxCodes at seven
/// (98px), so this row can never outgrow the screen.
constexpr int16_t kMarkW = 8, kMarkH = 4, kMarkPitch = 15, kMarkY = 212;
constexpr int16_t kScreenW = 240;

struct Prompt
{
    const char *line[4];
};

/**
 * @brief What to say, for each way there can be no code.
 *
 * Every line has to earn its place on a 240x240 round screen and fit one
 * 200px row, so these say where to go and nothing else -- with the README
 * carrying the rest.
 *
 * Blunter than they were. This app used to read the file itself and could
 * name the fault -- too big, not JSON, wrong schema. SDK::AppConfig reports
 * all of those as one unusable configuration and logs the detail where a
 * wearer cannot see it, so kNoConfig has to cover the lot and point at the
 * two places a code can come from instead of naming what is wrong.
 */
const Prompt &promptFor(Barcode::Problem problem)
{
    static const Prompt kNoConfig = {{ "No codes yet", "Set one in the", "UNA app, or write", "input.json" }};
    static const Prompt kNoValue  = {{ "input.json has", "no usable code", "", "" }};
    static const Prompt kNotSet   = {{ "No codes set yet", "Open the UNA app", "and enter your ID", "" }};
    static const Prompt kBadValue = {{ "That ID cannot", "be drawn: 1-16", "plain characters", "" }};

    switch (problem) {
    case Barcode::Problem::NoValue:  return kNoValue;
    case Barcode::Problem::NotSet:   return kNotSet;
    case Barcode::Problem::BadValue: return kBadValue;
    case Barcode::Problem::NoConfig:
    case Barcode::Problem::None:
        break;
    }
    return kNoConfig;
}

} // namespace

MainView::MainView()
    : mState(Barcode::makeUnsetState(Barcode::Problem::NoConfig))
    , mIndex(0)
    , idBuffer{}
    , captionBuffer{}
    , promptBuffer{}
{
    // Sized so its corners stay inside the round panel. The screen is a 240px
    // circle, so a rectangle centred on it may only be as tall as
    // 2*sqrt(120^2 - halfWidth^2): at 220 wide that is 95, and the 110 this
    // used to be put all four corners 3px into the bezel, where the display
    // cut the tips off. 94 leaves the furthest corner at radius 119.6.
    //
    // Height rather than width, because the white either side of the bars is
    // the barcode's quiet zone and a scanner needs it; the white above and
    // below is decoration. Code 128 has no vertical quiet zone requirement.
    barcodeBackground.setPosition(10, 73, 220, 94);
    barcodeBackground.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(barcodeBackground);

    barcode.setPosition(20, 75, 200, 90);
    add(barcode);

    caption.setPosition(kCaptionX, kCaptionY, kCaptionW, kCaptionH);
    caption.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    caption.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
    caption.setWildcard1(captionBuffer);
    caption.setVisible(false);
    add(caption);

    // Positioned in showPager() rather than here: the row is centred on the
    // face, so where each mark goes depends on how many there are.
    for (uint16_t i = 0; i < Barcode::kMaxCodes; i++) {
        pagerMark[i].setWidthHeight(kMarkW, kMarkH);
        pagerMark[i].setVisible(false);
        add(pagerMark[i]);
    }

    for (uint16_t i = 0; i < kPromptLines; i++) {
        promptLine[i].setPosition(kPromptX, kPromptTop + i * kPromptLineH, kPromptW, kPromptLineH);
        promptLine[i].setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
        promptLine[i].setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
        promptLine[i].setWildcard1(promptBuffer[i]);
        promptLine[i].setVisible(false);
        add(promptLine[i]);
    }
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
    switch (key) {
    case Gui::Config::Button::R2:
        presenter->exit();
        break;
    case Gui::Config::Button::L2:
        cycle(+1);
        break;
    case Gui::Config::Button::L1:
        cycle(-1);
        break;
    default:
        break;
    }
}

void MainView::cycle(int delta)
{
    if (mState.problem != Barcode::Problem::None || mState.count < 2) {
        return;
    }

    const int count = static_cast<int>(mState.count);
    mIndex = static_cast<uint8_t>(((static_cast<int>(mIndex) + delta) % count + count) % count);

    showBarcode();
    invalidate();
}

void MainView::onBarcodeChanged(const Barcode::State &state)
{
    mState = state;

    // A re-read can leave fewer codes than before, so never trust the old
    // position: an index past the end would draw somebody else's code.
    if (mIndex >= mState.count) {
        mIndex = 0;
    }

    if (mState.problem == Barcode::Problem::None && mState.count > 0) {
        showBarcode();
    } else {
        showPrompt(mState.problem);
    }

    invalidate();
}

void MainView::showBarcode()
{
    const Barcode::Code &code = mState.codes[mIndex];

    barcode.setCode(code.id);
    touchgfx::Unicode::strncpy(idBuffer, code.id, Barcode::kMaxIdLength + 1);

    // The name and nothing else. It used to read "<name> 2/6", which put the
    // position inside the label: "Gym 1/2" scans as a title with a fraction
    // in it rather than as the first of two codes. The pager marks below the
    // id carry the position now, so the caption is only ever the name -- and
    // an unnamed code has no caption at all.
    touchgfx::Unicode::strncpy(captionBuffer, code.name, kCaptionChars);

    showPager();

    // NO ON-SCREEN BUTTON HINTS. Not an oversight, and not a style choice:
    // the indicators are 23x35 ABGR2222 bitmaps and they blit corrupt on
    // device -- a smear of horizontal dashes where the arc should be. That
    // was found and worked around in 6b7de05 ("correct Barcode rendering on
    // device") by leaving every indicator NONE, and reintroduced here in
    // 0.3.0 by lighting L1/L2 for cycling, which put the same artifact on the
    // left edge instead of the bottom-right.
    //
    // The pager marks carry the affordance instead: four marks below the id
    // say there are four codes, drawn as plain filled boxes rather than
    // anything the blit path can corrupt.
    //
    // To have real hints back, the arcs have to be *drawn* rather than
    // blitted -- SleepLab and Squash replaced this container with one built
    // from touchgfx::Circle and PainterABGR2222 for exactly that reason.
    // Until then, leave these alone.
    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);

    barcodeBackground.setVisible(true);
    barcode.setVisible(true);
    textArea1.setVisible(true);
    caption.setVisible(code.name[0] != '\0');
    for (uint16_t i = 0; i < kPromptLines; i++) {
        promptLine[i].setVisible(false);
    }
}

void MainView::showPager()
{
    for (uint16_t i = 0; i < Barcode::kMaxCodes; i++) {
        pagerMark[i].setVisible(false);
    }

    // One code needs no pager: a single mark says nothing the screen does not
    // already say, and cycle() will not move off it anyway.
    if (mState.count < 2) {
        return;
    }

    const int16_t count = static_cast<int16_t>(mState.count);
    const int16_t left  = static_cast<int16_t>((kScreenW - ((count - 1) * kMarkPitch + kMarkW)) / 2);

    // 170 rather than something dimmer for the marks that are not current: the
    // panel is LCD8bpp_ABGR2222, two bits a channel, so the only greys that
    // exist are 85, 170 and 255. 85 is the dimmest non-black the display can
    // make and it is the first thing to wash out in daylight -- and a pager
    // whose unlit marks vanish has lost the one thing it is here to say, which
    // is how many codes there are. 170 stays legible and is still plainly not
    // the lit one.
    for (int16_t i = 0; i < count; i++) {
        pagerMark[i].setPosition(static_cast<int16_t>(left + i * kMarkPitch), kMarkY, kMarkW, kMarkH);
        pagerMark[i].setColor(i == static_cast<int16_t>(mIndex)
                                  ? touchgfx::Color::getColorFromRGB(255, 255, 255)
                                  : touchgfx::Color::getColorFromRGB(170, 170, 170));
        pagerMark[i].setVisible(true);
    }
}

void MainView::showPrompt(Barcode::Problem problem)
{
    // Nothing drawn and nothing left behind: a stale barcode sitting next to a
    // message saying there is no code is the one genuinely harmful thing this
    // screen could do -- it would still scan.
    barcodeBackground.setVisible(false);
    barcode.setVisible(false);
    textArea1.setVisible(false);
    caption.setVisible(false);
    for (uint16_t i = 0; i < Barcode::kMaxCodes; i++) {
        pagerMark[i].setVisible(false);
    }

    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);

    const Prompt &prompt = promptFor(problem);
    for (uint16_t i = 0; i < kPromptLines; i++) {
        touchgfx::Unicode::strncpy(promptBuffer[i], prompt.line[i], kPromptChars);
        promptLine[i].setVisible(true);
    }
}
