#ifndef TRACKFACESTATUS_HPP
#define TRACKFACESTATUS_HPP

#include <gui_generated/containers/TrackFaceStatusBase.hpp>
#include <SDK/GUI/SensorStatusRow.hpp>

/**
 * @brief Track face showing device status: current time of day and battery level.
 *
 * Intended as a low-information "status" face that the user can switch to during a session.
 */
class TrackFaceStatus : public TrackFaceStatusBase
{
public:
    TrackFaceStatus();
    virtual ~TrackFaceStatus() {}

    virtual void initialize();

    /**
     * @brief Display the current time of day.
     * @param h        Hour   (0-23).
     * @param m        Minute (0-59).
     * @param is12Hour When true, draw a 12-hour clock with an AM/PM suffix;
     *                 otherwise draw 24-hour time.
     */
    void setTime(uint8_t h, uint8_t m, bool is12Hour);

    /**
     * @brief Display the battery charge level.
     * @param level Charge percentage (0-100).
     */
    void setBatteryLevel(uint8_t level);

    /** @brief Set the external-HR icon state (see SensorStatusRow). */
    void setHr(SDK::Gui::SensorStatusRow::State state) { mSensorRow.setHr(state); }

    /**
     * @brief Name the sensor actually feeding heart rate, beside the icon.
     * @param source SDK::SensorDataParser::HeartRateEx::Source: 1 optical, 2 external.
     *
     * A letter rather than the icon's own steady-versus-flashing cue, because
     * that cue was already correct and was still misread: a strap that linked
     * before the session started and dropped two minutes in looked, to the
     * wearer, like a strap that was working. `E` and `O` cannot be misread.
     */
    void setHrSource(uint8_t source);

protected:
    SDK::Gui::SensorStatusRow mSensorRow;

    /// Gap between the heart icon and the source letter, px.
    static const int16_t kSourceGap = 6;
    /// Width allowed for the letter; one glyph of T_TMP_MEDIUM_18_L plus slack.
    static const int16_t kSourceW = 18;

    touchgfx::TextAreaWithOneWildcard mHrSource;
    touchgfx::Unicode::UnicodeChar    mHrSourceBuffer[2];

    // AM/PM suffix drawn next to the time in 12-hour format. Added here (rather
    // than in the generated base) so the layout stays in hand-written code; the
    // full ASCII wildcard range of every typography means the letters are
    // already present in the generated fonts, so no asset regeneration is needed.
    touchgfx::TextAreaWithOneWildcard mMeridiem;
    static const uint16_t MERIDIEM_SIZE = 3;                 // "AM"/"PM" + NUL
    touchgfx::Unicode::UnicodeChar mMeridiemBuffer[MERIDIEM_SIZE];

    // Time-of-day geometry (matches the clock-format design). The digits and the
    // AM/PM suffix are laid out as one group centred on the 240px-wide face, so a
    // two-digit hour widens the group and pushes the suffix to the right.
    //
    // kTimeY centres the 60px digit block vertically between the two dividers
    // (drawn at y=62 and y=136): the digit glyphs render across [Y+16, Y+60], so
    // Y=63 lands the block midway in the gap. kMeridiemY puts the 18px AM/PM
    // suffix on the same baseline as the digits: SemiBold-60 baseline is 60 and
    // Medium-18 baseline is 18, so the suffix box sits kTimeY + (60 - 18) = +42.
    static const int16_t  kTimeY       = 63;
    static const int16_t  kTimeH       = 77;
    static const int16_t  kMeridiemY   = 105;
    static const int16_t  kMeridiemH   = 27;
    static const int16_t  kMeridiemGap = 5;   // digits -> AM/PM spacing (px)
};

#endif // TRACKFACESTATUS_HPP
