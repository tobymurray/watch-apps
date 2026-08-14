#include <gui/containers/PackListItem.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

namespace {

/// Width given to the state column. Measured, not guessed: a 62px column
/// rendered "scanning" as "canning", clipped on its left by the right-aligned
/// text area, and widening it far enough to fit whole words starved the name
/// beside it. The states are abbreviated instead -- four characters at this
/// typography, which 44px holds with room to spare.
constexpr int16_t kStateWidth = 44;
constexpr int16_t kGap        = 6;

/// The focused row is white; the rows around it are dimmed so the eye lands
/// on the selection without needing a highlight bar behind it -- which this
/// display's 2-bits-per-channel framebuffer would band badly anyway.
constexpr uint8_t kFocusedLevel = 255;
constexpr uint8_t kContextLevel = 128;

/// Abbreviated deliberately: every character this column takes is one the name
/// beside it does not get, and the name is where two packs have to be told
/// apart. The two that matter are the two that are not "fine" -- so those keep
/// upper case, which reads as a flag rather than as a status.
const char *stateWord(uint8_t state)
{
    switch (static_cast<CustomMessage::PackState>(state)) {
        case CustomMessage::PackState::Pending:    return "wait";
        case CustomMessage::PackState::Scanning:   return "scan";
        case CustomMessage::PackState::Verified:   return "ok";
        case CustomMessage::PackState::Mismatched: return "BAD";
        case CustomMessage::PackState::Unreadable: return "ERR";
    }
    // A state this build does not know about is a newer service talking to an
    // older GUI. Say so rather than guess a verdict.
    return "?";
}

} // namespace

PackListItem::PackListItem()
{
    setWidth(kWidth);
    setHeight(kHeight);

    // Vertically centred within the row: the 16px typography sits in 28px of
    // row, so 6px of lead centres it.
    mName.setPosition(0, 6, kWidth - kStateWidth - kGap, 18);
    mName.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16_L));
    // Plain clip, not an ellipsis: the double-ellipsis action spent six
    // characters' width drawing dots, which on a column this narrow cost more
    // information than the truncation it was announcing.
    mName.setWideTextAction(touchgfx::WIDE_TEXT_NONE);
    mNameBuf[0] = 0;
    mName.setWildcard(mNameBuf);
    add(mName);

    mState.setPosition(kWidth - kStateWidth, 6, kStateWidth, 18);
    mState.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16_R));
    mStateBuf[0] = 0;
    mState.setWildcard(mStateBuf);
    add(mState);

    clear();
}

void PackListItem::setPack(const char *name, uint8_t state, bool focused)
{
    touchgfx::Unicode::strncpy(mNameBuf, name, kNameBufSize - 1);
    mNameBuf[kNameBufSize - 1] = 0;

    touchgfx::Unicode::strncpy(mStateBuf, stateWord(state), kStateBufSize - 1);
    mStateBuf[kStateBufSize - 1] = 0;

    const uint8_t level = focused ? kFocusedLevel : kContextLevel;
    const touchgfx::colortype colour = touchgfx::Color::getColorFromRGB(level, level, level);
    mName.setColor(colour);
    mState.setColor(colour);

    mName.setVisible(true);
    mState.setVisible(true);
    invalidate();
}

void PackListItem::clear()
{
    mNameBuf[0]  = 0;
    mStateBuf[0] = 0;
    mName.setVisible(false);
    mState.setVisible(false);
    invalidate();
}
