#include <gui/main_screen/MainView.hpp>

#include <cstdio>
#include <cstring>

#include <texts/TextKeysAndLanguages.hpp>

#include "BarcodeLayout.hpp"
#include "Symbology.hpp"

namespace
{

/// Four 18pt lines, centred on the face and inset far enough to clear the
/// corners the round display cuts off.
constexpr int16_t kPromptX = 20, kPromptW = 200, kPromptLineH = 24, kPromptTop = 72;

/// The caption sits in the band above the bars, inset past the button ticks
/// the bezel container draws down either side. The numbers moved to
/// BarcodeLayout.hpp when QR arrived: a square symbol has to be laid out
/// against this band, and it turns out there is no room for both.
using BarcodeLayout::kCaptionX;
using BarcodeLayout::kCaptionY;
using BarcodeLayout::kCaptionW;
using BarcodeLayout::kCaptionH;

/// The id row, sized to the circle rather than to the screen. The generated
/// textArea1 is 203px wide at x=19 -- 203px of a *square*. The id sits low on a
/// round panel, though, so the bezel takes the ends of a long one: masked
/// renders put 20 pixels of a 15-character id outside the circle and 64 of a
/// 16-character one.
///
/// Both numbers below come from measuring, because neither font metrics nor the
/// circle's arithmetic predict it well -- a glyph's widest point is not on its
/// bottom row, so the naive bound is too pessimistic. Advance widths from
/// Font::getStringWidth, ink from the rendered pixels, "outside" against a
/// 120px-radius mask:
///
///   id                 chars  bold  reg   ink  outside
///   0123456789ABC        13    172  147   170     0
///   0123456789ABCD       14    186  160   185     0
///   0123456789ABCDE      15    197  169   195    20
///   0123456789ABCDEF     16    208  178   203    64
///   WWWWWWWWWWWW         12    240  216   203    29
///
/// So kIdInkLimit is 186: the widest string *measured* to sit wholly inside the
/// circle, not an interpolation between 186 and the 197 that fails. Above it the
/// 18pt face is used instead, which is why 15 and 16 characters now fit at all.
/// kIdW then clips whatever still will not fit -- twelve 'W's beat even 18pt --
/// so ink outside the panel is impossible whatever an id says.
///
/// The simulator cannot show any of this: it draws the full 240x240 square with
/// no bezel, so its output has to be masked with a 120px-radius disc before the
/// clipping appears. That is why this reached the watch before it was caught.
constexpr int16_t  kIdX = 27, kIdY = 178, kIdW = 187, kIdH = 30;
constexpr uint16_t kIdInkLimit = 186;

/// Where the id goes when even 18pt will not fit it on one line. Two lines 18px
/// apart, which puts the first line's ink at 174..186 and the second's at
/// 192..204: 7px below the bars' white backing and 8px above the pager marks.
/// Both rows are comfortably inside the arc -- the lower one allows about 170px
/// and no half can exceed 144, since the widest possible half is eight 'W's.
///
/// Splitting rather than cutting, because the id under the bars is what a
/// person reads out or types in when a scanner will not cooperate, and half an
/// id is no use for that. The bars always carry the whole thing; this makes the
/// text match them. `WWWWWWWWWWWW` is 216px at 18pt and lost its last two
/// characters to kIdW before this existed.
constexpr int16_t kIdLine1Y = 169, kIdLine2Y = 187, kIdLineH = 18;

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
 * @brief "Set it to Code128, QRCode or ITF", word-wrapped to fit.
 *
 * Built from Barcode::kFormatNames rather than spelled out here, because that
 * is exactly what fell out of sync when ITF arrived: an encoder, an accepted
 * `fmtN` value and a manifest pattern all showed up together, and this prompt
 * did not, because nothing tied it to the others. Growing kFormatNames is now
 * the only step -- this reflows around however long the list gets.
 *
 * Wrapped by measured width rather than a fixed character count, the same way
 * MainView::layOutId() sizes the id row: Regular 18 is proportional, so a
 * character budget would wrap "IIII" and "WWWW" alike when only one of them
 * is actually too wide for the 200px prompt column.
 */
const char *const *badFormatLines()
{
    static char lines[3][24];
    for (auto &line : lines) {
        line[0] = '\0';
    }

    char joined[96] = "Set it to ";
    for (uint8_t i = 0; i < Barcode::kFormatCount; i++) {
        if (i > 0) {
            std::strcat(joined, i == Barcode::kFormatCount - 1 ? " or " : ", ");
        }
        std::strcat(joined, Barcode::kFormatNames[i]);
    }

    // getStringWidth() takes UnicodeChar, not char -- the same conversion
    // MainView::layOutId() does for the id row, via the const char* overload
    // of Unicode::strncpy().
    const touchgfx::Font *font = touchgfx::TypedText(T_TMP_REGULAR_18).getFont();
    // Sized well past the 24-char display lines: this is only the scratch
    // buffer a candidate line is measured in before being (safely) truncated
    // into one of those, so it is not itself the thing that has to fit.
    char attempt[64];
    touchgfx::Unicode::UnicodeChar measureBuf[64];
    uint8_t lineIndex = 0;
    char *word = std::strtok(joined, " ");
    while (word != nullptr && lineIndex < 3) {
        if (lines[lineIndex][0] == '\0') {
            std::strncpy(attempt, word, sizeof(attempt) - 1);
            attempt[sizeof(attempt) - 1] = '\0';
        } else {
            std::snprintf(attempt, sizeof(attempt), "%s %s", lines[lineIndex], word);
        }
        touchgfx::Unicode::strncpy(measureBuf, attempt, sizeof(measureBuf) / sizeof(measureBuf[0]));

        if (lines[lineIndex][0] == '\0'
                || font->getStringWidth(touchgfx::TEXT_DIRECTION_LTR, measureBuf) <= kPromptW) {
            std::strncpy(lines[lineIndex], attempt, sizeof(lines[lineIndex]) - 1);
            lines[lineIndex][sizeof(lines[lineIndex]) - 1] = '\0';
            word = std::strtok(nullptr, " ");
        } else {
            // Does not fit after all: leave it for the next line instead of
            // consuming it, so a long name is never silently dropped.
            lineIndex++;
        }
    }

    static const char *result[3] = { lines[0], lines[1], lines[2] };
    return result;
}

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
    // ITF is the only digit-only format, so naming it is more useful than a
    // generic "this format" would be -- there is exactly one place to go
    // fix it, and this says which.
    static const Prompt kBadDigitCount = {{ "ITF needs an", "even count of", "digits, 2 to 16", "" }};
    static const Prompt kBadCharacters = {{ "ITF only draws", "digits 0-9", "", "" }};
    // Every format accepts a space -- it is ordinary printable ASCII -- so
    // this is not "that ID cannot be drawn" and does not share its words: a
    // wearer who sees "1-16 plain characters" would have no reason to
    // suspect a space they cannot even see on the phone form.
    static const Prompt kBadWhitespace = {{ "That ID starts", "or ends with a", "space, remove it", "" }};

    switch (problem) {
    case Barcode::Problem::NoValue:  return kNoValue;
    case Barcode::Problem::NotSet:   return kNotSet;
    case Barcode::Problem::BadValue: return kBadValue;
    case Barcode::Problem::BadDigitCount: return kBadDigitCount;
    case Barcode::Problem::BadCharacters: return kBadCharacters;
    case Barcode::Problem::BadWhitespace: return kBadWhitespace;
    case Barcode::Problem::BadFormat: {
        // Only reachable from a hand-edited input.json: the phone's pattern
        // offers nothing else. It still gets its own words, because "that ID
        // cannot be drawn" would send the wearer to look at an id that is
        // perfectly fine.
        const char *const *lines = badFormatLines();
        static const Prompt kBadFormat = {{ "Unknown format.", lines[0], lines[1], lines[2] }};
        return kBadFormat;
    }
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
    // Geometry lives in BarcodeLayout.hpp rather than here, because these
    // numbers answer to the panel -- round, and four levels a channel -- and
    // that is an argument the host tests need to be able to hold them to.
    //
    // The backing's height is the constrained number, not its width: the panel
    // is a circle, so a rectangle centred on it may only be as tall as
    // 2*sqrt(r^2 - halfWidth^2). At 220 wide that is 95, and the 110 this used
    // to be put all four corners 3px into the bezel, where the display cut the
    // tips off.
    //
    // Height rather than width, because the white either side of the bars is
    // the barcode's quiet zone and a scanner needs it; the white above and
    // below is decoration. Code 128 has no vertical quiet zone requirement.
    barcodeBackground.setPosition(BarcodeLayout::kBackingX, BarcodeLayout::kBackingY,
                                  BarcodeLayout::kBackingW, BarcodeLayout::kBackingH);
    barcodeBackground.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(barcodeBackground);

    barcode.setPosition(BarcodeLayout::kBarsX, BarcodeLayout::kBarsY,
                        BarcodeLayout::kBarsW, BarcodeLayout::kBarsH);
    add(barcode);

    // Square, and much higher on the face than the bars: it has to end above
    // the id row, and the largest square that does is 144px against this one's
    // 132. BarcodeLayout::kQrY has the arithmetic and why 5px a module misses
    // by a single pixel row. No backing box -- the widget's own rect is the
    // quiet zone.
    qr.setPosition(BarcodeLayout::kQrX, BarcodeLayout::kQrY,
                   BarcodeLayout::kQrSide, BarcodeLayout::kQrSide);
    qr.setVisible(false);
    add(qr);

    // Re-placed from the generated 203px-wide box: see kIdX above. The widget
    // itself belongs to MainViewBase, which the Designer owns and rewrites.
    textArea1.setPosition(kIdX, kIdY, kIdW, kIdH);

    caption.setPosition(kCaptionX, kCaptionY, kCaptionW, kCaptionH);
    caption.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    caption.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
    caption.setWildcard1(captionBuffer);
    caption.setVisible(false);
    add(caption);

    // Positioned in layOutId(), which knows whether there are two lines.
    idLine2.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    idLine2.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
    idLine2.setWildcard1(idLine2Buffer);
    idLine2.setVisible(false);
    add(idLine2);

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

    // Which of the two widgets draws this one. Asked of the symbology rather
    // than of the format directly, so a second linear format would need no
    // change here and a second matrix one would need only its encoder.
    const bool matrix = Barcode::isMatrix(code.format);
    if (matrix) {
        qr.setCode(code.format, code.id);
    } else {
        barcode.setCode(code.format, code.id);
    }

    // layOutId fills idBuffer -- with the whole id, or with its first half when
    // it has to split -- so nothing else may write that buffer.
    const bool splitId = layOutId(code.id);

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

    // Exactly one of the two, and the other explicitly hidden rather than
    // merely not shown: cycling from a QR code to a Code 128 one must not
    // leave the previous symbol on the glass, because a stale symbol still
    // scans.
    barcodeBackground.setVisible(!matrix);
    barcode.setVisible(!matrix);
    qr.setVisible(matrix);

    textArea1.setVisible(true);
    idLine2.setVisible(splitId);

    // **A QR code shows no name.** Not a preference: every QR size that fits
    // above the id row overlaps the caption band, and keeping the caption
    // would mean version 1 at a 3px module -- a third off the module size and
    // two thirds off the error correction. See BarcodeLayout::kQrY and
    // Docs/QR.md. The id text and the pager marks still say which code this is.
    caption.setVisible(!matrix && code.name[0] != '\0');

    for (uint16_t i = 0; i < kPromptLines; i++) {
        promptLine[i].setVisible(false);
    }
}

bool MainView::layOutId(const char *id)
{
    // Measured per string rather than by character count, because the font is
    // proportional: sixteen '1's are narrower than fourteen mixed characters,
    // so a length rule would shrink narrow ids for nothing and still overflow
    // on wide ones. Twelve 'W's is only twelve characters and 240px.
    //
    // Regular 18 is already in flash for the caption and the prompts, so
    // stepping down to it costs no font -- which matters, since 594bedb trimmed
    // this app to the two faces it actually draws.
    const touchgfx::TypedText large(T_TMP_SEMIBOLD_20);
    const touchgfx::TypedText small(T_TMP_REGULAR_18);

    touchgfx::Unicode::strncpy(idBuffer, id, Barcode::kMaxIdLength + 1);
    const uint16_t largeWidth = large.getFont()->getStringWidth(touchgfx::TEXT_DIRECTION_LTR, idBuffer);
    const uint16_t smallWidth = small.getFont()->getStringWidth(touchgfx::TEXT_DIRECTION_LTR, idBuffer);

    if (smallWidth <= kIdW) {
        textArea1.setTypedText(largeWidth <= kIdInkLimit ? large : small);
        textArea1.setPosition(kIdX, kIdY, kIdW, kIdH);
        idLine2.setVisible(false);
        return false;
    }

    // Split by character count, not by width. Both halves fit either way -- the
    // widest half possible is 144px against the 170 the lower row allows -- and
    // an even character count is what a person reading the id back expects to
    // see. The odd character goes on the first line, in reading order.
    const size_t length = std::strlen(id);
    const size_t first  = (length + 1) / 2;

    touchgfx::Unicode::strncpy(idBuffer, id, static_cast<uint16_t>(first + 1));
    idBuffer[first] = 0;
    touchgfx::Unicode::strncpy(idLine2Buffer, id + first, Barcode::kMaxIdLength + 1);

    textArea1.setTypedText(small);
    idLine2.setTypedText(small);
    textArea1.setPosition(kIdX, kIdLine1Y, kIdW, kIdLineH);
    idLine2.setPosition(kIdX, kIdLine2Y, kIdW, kIdLineH);
    idLine2.setVisible(true);
    return true;
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
    qr.setVisible(false);
    textArea1.setVisible(false);
    idLine2.setVisible(false);
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
