#include <gui/track_screen/TrackView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include <cstring>

using FaceId = App::MenuNav::TrackView::Id;

TrackView::TrackView()
{

}

void TrackView::setupScreen()
{
    TrackViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::AMBER);

    scrollIndicator.setConfig(ScrollIndicator::kSmall);
    scrollIndicator.setCount(FaceId::ID_COUNT);

    // Added last so it sits over whichever face is showing. Kept inside the
    // round bezel: a full-width band would have its ends clipped away, so the
    // band is inset and the text centred within it.
    mBannerBg.setPosition(kBannerX, kBannerY, kBannerW, kBannerH);
    mBannerBg.setColor(touchgfx::Color::getColorFromRGB(170, 85, 0));
    mBannerBg.setVisible(false);
    add(mBannerBg);

    mBanner.setPosition(kBannerX, kBannerY + 2, kBannerW, kBannerH);
    mBanner.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mBanner.setLinespacing(0);
    mBanner.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_18));
    mBannerBuffer[0] = 0;
    mBanner.setWildcard(mBannerBuffer);
    mBanner.setVisible(false);
    add(mBanner);
}

void TrackView::showHrStrapLost(uint16_t /*secondsWithout*/)
{
    // The seconds are in Debug/squash.log with the moment it happened; on the
    // glass the number would only invite the wearer to wonder whether 6 is
    // worse than 5. What they can act on is that it is gone.
    touchgfx::Unicode::snprintf(mBannerBuffer, sizeof(mBannerBuffer) / sizeof(mBannerBuffer[0]),
                                "HR STRAP LOST");
    mBanner.setWildcard(mBannerBuffer);

    mBannerTicksLeft = kBannerTicks;
    mBannerBg.setVisible(true);
    mBanner.setVisible(true);
    mBannerBg.invalidate();
    mBanner.invalidate();
}

void TrackView::handleTickEvent()
{
    TrackViewBase::handleTickEvent();

    if (mBannerTicksLeft == 0) {
        return;
    }
    if (--mBannerTicksLeft == 0) {
        mBannerBg.setVisible(false);
        mBanner.setVisible(false);
        mBannerBg.invalidate();
        mBanner.invalidate();
    }
}

void TrackView::tearDownScreen()
{
    TrackViewBase::tearDownScreen();
}

void TrackView::setPositionId(uint16_t id)
{
    if (id < static_cast<uint16_t>(FaceId::ID_TRACK1)) {
        id = FaceId::ID_TRACK1;
    }
    if (id >= FaceId::ID_COUNT) {
        id = FaceId::ID_COUNT - 1;
    }
    mCurrentFaceId = id;

    trackFaceTotal.setVisible(false);
    trackFaceLap.setVisible(false);
    trackFaceStatus.setVisible(false);

    scrollIndicator.setActiveId(id);

    switch (id) {
        case FaceId::ID_TRACK1:     trackFaceTotal.setVisible(true);      break;
        case FaceId::ID_TRACK2:     trackFaceStatus.setVisible(true);     break;
        default: break;
    }

    trackFaceTotal.invalidate();
    trackFaceLap.invalidate();
    trackFaceStatus.invalidate();
}

uint16_t TrackView::getPositionId()
{
    return mCurrentFaceId;
}

void TrackView::setConfig(bool isImperial, const uint8_t* thresholds, uint8_t thresholdCount)
{
    mIsImperial        = isImperial;
    mHrThresholdCount  = thresholdCount < App::Config::kHrThresholdsCount ? 
                            thresholdCount : static_cast<uint8_t>(App::Config::kHrThresholdsCount);
    memcpy(mHrThresholds, thresholds, mHrThresholdCount);
}

void TrackView::setTimeFormat(bool is12Hour)
{
    mIs12Hour = is12Hour;
}

void TrackView::setTrackData(const Track::Data& data)
{
    trackFaceTotal.setHR(data.hr, mHrThresholds, mHrThresholdCount);
    trackFaceTotal.setCalories(data.totalCalories);
    trackFaceTotal.setTimer(data.totalTime);

    trackFaceLap.setLapAvgHR(data.avgLapHR);
    trackFaceLap.setLapNumber(data.lapNum);
    trackFaceLap.setTimer(data.lapTime);

    mHrSource = data.hrSource;
    updateHrIcon();
}

void TrackView::setTime(uint8_t h, uint8_t m)
{
    trackFaceStatus.setTime(h, m, mIs12Hour);
}

void TrackView::setBatteryLevel(uint8_t level)
{
    trackFaceStatus.setBatteryLevel(level);
}

void TrackView::setAccessoryStatus(uint8_t state)
{
    mAccessoryState = state;
    updateHrIcon();
}

void TrackView::updateHrIcon()
{
    // In-activity: icon follows the live HR source, not the raw link state.
    trackFaceStatus.setHr(SDK::Gui::SensorStatusRow::hrStateFromSource(
            mAccessoryState, mHrSource));
    trackFaceStatus.setHrSource(mHrSource);
}

void TrackView::handleKeyEvent(uint8_t key)
{
    if (key == SDK::GUI::Button::L1) {
        uint16_t p = mCurrentFaceId;
        if (p <= FaceId::ID_TRACK1) {
            p = FaceId::ID_COUNT - 1;
        } else {
            p--;
        }
        setPositionId(p);
    }

    if (key == SDK::GUI::Button::L2) {
        uint16_t p = mCurrentFaceId + 1u;
        if (p >= FaceId::ID_COUNT) {
            p = FaceId::ID_TRACK1;
        }
        setPositionId(p);
    }

    if (key == SDK::GUI::Button::R1) {
        application().gotoTrackActionScreenNoTransition();
    }

    if (key == SDK::GUI::Button::R2) {
        presenter->saveLap();
        application().gotoTrackLapScreenNoTransition();
    }
}
