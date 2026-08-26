#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>

namespace
{

/// Four 18pt lines, centred on the face and inset far enough to clear the
/// corners the round display cuts off.
constexpr int16_t kPromptX = 20, kPromptW = 200, kPromptLineH = 24, kPromptTop = 72;

struct Prompt
{
    const char *line[4];
};

/**
 * @brief What to say, for each way there can be no id.
 *
 * Every line has to earn its place on a 240x240 round screen and fit one
 * 200px row, so these say where to go and nothing else -- with the README
 * carrying the rest.
 *
 * Blunter than they were. This app used to read the file itself and could
 * name the fault -- too big, not JSON, wrong schema. SDK::AppConfig reports
 * all of those as one unusable configuration and logs the detail where a
 * wearer cannot see it, so kNoConfig has to cover the lot and point at the
 * two places an id can come from instead of naming what is wrong.
 */
const Prompt &promptFor(Barcode::Problem problem)
{
    static const Prompt kNoConfig = {{ "No id yet", "Set one in the", "UNA app, or write", "input.json" }};
    static const Prompt kNoValue  = {{ "input.json has", "no usable id", "", "" }};
    static const Prompt kNotSet   = {{ "No id set yet", "Open the UNA app", "and enter the", "number on yours" }};
    static const Prompt kBadValue = {{ "That id cannot", "be drawn: 1-16", "plain characters", "" }};

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
    : idBuffer{}
    , promptBuffer{}
{
    barcodeBackground.setPosition(10, 65, 220, 110);
    barcodeBackground.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(barcodeBackground);

    barcode.setPosition(20, 75, 200, 90);
    add(barcode);

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
    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}

void MainView::onBarcodeChanged(const Barcode::State &state)
{
    if (state.problem == Barcode::Problem::None) {
        showBarcode(state);
    } else {
        showPrompt(state.problem);
    }

    invalidate();
}

void MainView::showBarcode(const Barcode::State &state)
{
    barcode.setCode(state.id);
    touchgfx::Unicode::strncpy(idBuffer, state.id, Barcode::kMaxIdLength + 1);

    barcodeBackground.setVisible(true);
    barcode.setVisible(true);
    textArea1.setVisible(true);
    for (uint16_t i = 0; i < kPromptLines; i++) {
        promptLine[i].setVisible(false);
    }
}

void MainView::showPrompt(Barcode::Problem problem)
{
    // Nothing drawn and nothing left behind: a stale barcode sitting next to a
    // message saying there is no id is the one genuinely harmful thing this
    // screen could do -- it would still scan.
    barcodeBackground.setVisible(false);
    barcode.setVisible(false);
    textArea1.setVisible(false);

    const Prompt &prompt = promptFor(problem);
    for (uint16_t i = 0; i < kPromptLines; i++) {
        touchgfx::Unicode::strncpy(promptBuffer[i], prompt.line[i], kPromptChars);
        promptLine[i].setVisible(true);
    }
}
