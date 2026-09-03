#include <gui/containers/TrackFaceStatus.hpp>
#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/Utils/ClockTime.hpp"

TrackFaceStatus::TrackFaceStatus()
{
}

void TrackFaceStatus::initialize()
{
    TrackFaceStatusBase::initialize();

    // Draw the time left-aligned (the generated base centres it) so the AM/PM
    // suffix can follow the digits; setTime() re-centres the whole group.
    dayTimeValue.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_60_L));
    dayTimeValue.setY(kTimeY);

    // AM/PM suffix, shown only in 12-hour format.
    mMeridiem.setColor(touchgfx::Color::getColorFromRGB(192, 192, 192));
    mMeridiem.setLinespacing(0);
    mMeridiem.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18_L));
    mMeridiem.setWildcard(mMeridiemBuffer);
    mMeridiem.setVisible(false);
    add(mMeridiem);

    // External-HR status icon above the time of day (HR-only app: no GPS icon).
    add(mSensorRow);
    mSensorRow.setPosition(0, 20, 240, 24);
    mSensorRow.setIcons(touchgfx::BITMAP_INVALID, touchgfx::BITMAP_INVALID,
                        BITMAP_SENSORHRDARK_ID, BITMAP_SENSORHRLIGHT_ID);

    // The letter sits immediately right of the icon. SensorStatusRow centres a
    // lone icon in its own width, so the icon's right edge is derived from the
    // bitmap rather than assumed -- the two must not disagree about where the
    // heart ends.
    const int16_t iconW = touchgfx::Bitmap(BITMAP_SENSORHRLIGHT_ID).getWidth();
    const int16_t letterX = static_cast<int16_t>((mSensorRow.getWidth() + iconW) / 2 + kSourceGap);

    mHrSource.setColor(touchgfx::Color::getColorFromRGB(192, 192, 192));
    mHrSource.setLinespacing(0);
    mHrSource.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18_L));
    mHrSourceBuffer[0] = 0;
    mHrSource.setWildcard(mHrSourceBuffer);
    mHrSource.setPosition(letterX, 20, kSourceW, 24);
    add(mHrSource);
}

void TrackFaceStatus::setHrSource(uint8_t source)
{
    // SDK::SensorDataParser::HeartRateEx::Source; anything else is "no source
    // yet", which is a real state at the start of a session and is shown as one
    // rather than left blank.
    const touchgfx::Unicode::UnicodeChar letter =
        (source == 2) ? 'E' : ((source == 1) ? 'O' : '-');

    if (mHrSourceBuffer[0] == letter) {
        return;
    }

    mHrSource.invalidate();
    mHrSourceBuffer[0] = letter;
    mHrSourceBuffer[1] = 0;
    mHrSource.setWildcard(mHrSourceBuffer);
    mHrSource.invalidate();
}

void TrackFaceStatus::setTime(uint8_t h, uint8_t m, bool is12Hour)
{
    // Invalidate the old glyph rectangles before the text and positions move.
    dayTimeValue.invalidate();
    mMeridiem.invalidate();

    const SDK::Clock::Hour12 civil = SDK::Clock::to12Hour(h);
    const uint8_t hour = is12Hour ? civil.hour : h;
    const bool    pm   = civil.pm;

    Unicode::snprintf(dayTimeValueBuffer, DAYTIMEVALUE_SIZE, "%u:%02u", hour, m);
    dayTimeValue.setWildcard(dayTimeValueBuffer);
    const uint16_t timeW = dayTimeValue.getTextWidth();

    uint16_t merW   = 0;
    uint16_t groupW = timeW;
    if (is12Hour) {
        mMeridiemBuffer[0] = pm ? 'P' : 'A';
        mMeridiemBuffer[1] = 'M';
        mMeridiemBuffer[2] = 0;
        mMeridiem.setWildcard(mMeridiemBuffer);
        merW   = mMeridiem.getTextWidth();
        groupW = static_cast<uint16_t>(timeW + kMeridiemGap + merW);
    }

    // Centre the digits (+ suffix) as one group on the 240px face.
    const int16_t groupLeft = static_cast<int16_t>((getWidth() - groupW) / 2);

    dayTimeValue.setPosition(groupLeft, kTimeY, static_cast<int16_t>(timeW + 4), kTimeH);
    dayTimeValue.invalidate();

    if (is12Hour) {
        mMeridiem.setPosition(static_cast<int16_t>(groupLeft + timeW + kMeridiemGap),
                              kMeridiemY, static_cast<int16_t>(merW + 4), kMeridiemH);
        mMeridiem.setVisible(true);
        mMeridiem.invalidate();
    } else {
        mMeridiem.setVisible(false);
    }
}

void TrackFaceStatus::setBatteryLevel(uint8_t level)
{
    battery.setLevel(level);

    Unicode::snprintf(percentValueBuffer, PERCENTVALUE_SIZE, "%u%s",
        level, touchgfx::TypedText(T_TEXT_PERCENT).getText());
    percentValue.invalidate();
}
