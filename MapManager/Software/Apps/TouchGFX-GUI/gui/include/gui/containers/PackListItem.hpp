#ifndef PACKLISTITEM_HPP
#define PACKLISTITEM_HPP

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

#include "Commands.hpp"

/**
 * @class PackListItem
 * @brief One row of the pack list: the pack's name, and where it has got to.
 *
 * Hand-built rather than Designer-generated, for the same reason MainView's
 * own two lines are: the inherited LapListItem's wildcard buffers are 10 and
 * 12 UnicodeChars, sized for "Lap 3" and a lap time, and a pack name runs to
 * 31 characters ("previous-20260813161118.rawtiles"). Truncating to 10 would
 * render athens-urban-v1 and athens-urban-v3 identical, which is worse than
 * showing nothing -- so the row carries its own buffers instead.
 *
 * Two text areas rather than one composed string, so the state stays in a
 * column the eye can run down: the name is left-aligned and the state
 * right-aligned, using the Left/Right typography variants the text database
 * already provides.
 */
class PackListItem : public touchgfx::Container
{
public:
    /// Row geometry. Five rows of 28px span y=50..190 when centred on the
    /// panel, and at 70px from centre the round bezel still leaves a ~195px
    /// chord -- so 190px of row clears it at both ends, top row and bottom
    /// row alike. Worth taking every pixel of that: at 180px the name column
    /// cut "athens-urban-v1" one character short of the digit that tells it
    /// from "athens-urban-v3", which is the whole point of showing a name.
    /// Seven rows would reach y=22, where only ~138px is left -- not enough.
    static constexpr int16_t kWidth  = 190;
    static constexpr int16_t kHeight = 28;

    PackListItem();
    virtual ~PackListItem() {}

    /**
     * @brief Fill the row for one pack.
     * @param name    The pack's filename.
     * @param state   A CustomMessage::PackState.
     * @param focused Whether this is the row the list has selected -- shown
     *                brighter, since the dimmed rows around it are context.
     */
    void setPack(const char *name, uint8_t state, bool focused);

    /**
     * @brief Blank the row.
     *
     * The list binds one more drawable than it can show, so a spare is always
     * pointed at an index past the end of the data while scrolling.
     */
    void clear();

private:
    /// Long enough for the longest name a roster row can carry, so the row
    /// never truncates before the text engine does.
    static constexpr uint16_t kNameBufSize  = CustomMessage::kMaxRowNameLen;
    static constexpr uint16_t kStateBufSize = 12;

    touchgfx::TextAreaWithOneWildcard mName;
    touchgfx::TextAreaWithOneWildcard mState;
    touchgfx::Unicode::UnicodeChar    mNameBuf[kNameBufSize];
    touchgfx::Unicode::UnicodeChar    mStateBuf[kStateBufSize];
};

#endif // PACKLISTITEM_HPP
