#include <gui/main_screen/MainView.hpp>

#include <cstdio>

#include <texts/TextKeysAndLanguages.hpp>

namespace
{

/// Four 18pt lines, centred on the face and inset far enough to clear the
/// corners the round display cuts off.
constexpr int16_t kPromptX = 20, kPromptW = 200, kPromptLineH = 24, kPromptTop = 72;

/// The caption sits in the band above the bars, inset past the button ticks
/// the bezel container draws down either side.
constexpr int16_t kCaptionX = 40, kCaptionY = 48, kCaptionW = 160, kCaptionH = 24;

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

/// "Kids 2/5", or "2/5" unnamed, or just the name when there is only one code.
void composeCaption(const Barcode::State &state, uint8_t index,
                    char *out, size_t outSize)
{
    const char *name = state.codes[index].name;

    if (state.count <= 1) {
        snprintf(out, outSize, "%s", name);
        return;
    }
    if (name[0] == '\0') {
        snprintf(out, outSize, "%u/%u", static_cast<unsigned>(index + 1),
                 static_cast<unsigned>(state.count));
        return;
    }
    snprintf(out, outSize, "%s %u/%u", name, static_cast<unsigned>(index + 1),
             static_cast<unsigned>(state.count));
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

    char text[kCaptionChars] {};
    composeCaption(mState, mIndex, text, sizeof(text));
    touchgfx::Unicode::strncpy(captionBuffer, text, kCaptionChars);

    // The hints only appear when there is somewhere to go, so a single code
    // does not advertise a button that would do nothing.
    const bool cyclable = mState.count > 1;
    buttons.setL1(cyclable ? ButtonsSet::WHITE : ButtonsSet::NONE);
    buttons.setL2(cyclable ? ButtonsSet::WHITE : ButtonsSet::NONE);

    barcodeBackground.setVisible(true);
    barcode.setVisible(true);
    textArea1.setVisible(true);
    caption.setVisible(text[0] != '\0');
    for (uint16_t i = 0; i < kPromptLines; i++) {
        promptLine[i].setVisible(false);
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

    buttons.setL1(ButtonsSet::NONE);
    buttons.setL2(ButtonsSet::NONE);

    const Prompt &prompt = promptFor(problem);
    for (uint16_t i = 0; i < kPromptLines; i++) {
        touchgfx::Unicode::strncpy(promptBuffer[i], prompt.line[i], kPromptChars);
        promptLine[i].setVisible(true);
    }
}
