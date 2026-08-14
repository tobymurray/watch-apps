#include <gui/track_screen/TrackView.hpp>
#include <SDK/Utils/Utils.hpp>
#include <cstring>

using FaceId = App::MenuNav::TrackView::Id;

TrackView::TrackView()
{

}

void TrackView::setupScreen()
{
    TrackViewBase::setupScreen();

    // The map face is added here rather than in the generated base: it is this
    // subclass's member, and setupScreen is the only place a transient screen
    // can re-borrow the Model's map session.
    mFaceMap.setSources(presenter->mapSession());
    mFaceMap.setVisible(false);
    add(mFaceMap);

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::AMBER);

    scrollIndicator.setConfig(ScrollIndicator::kSmall);
    scrollIndicator.setCount(FaceId::ID_COUNT);
}

void TrackView::tearDownScreen()
{
    TrackViewBase::tearDownScreen();
}

void TrackView::setPositionId(uint16_t id)
{
    if (id >= FaceId::ID_COUNT) {
        id = FaceId::ID_COUNT - 1;
    }
    mCurrentFaceId = id;

    trackFaceOverview.setVisible(false);
    trackFaceTotal.setVisible(false);
    trackFaceLap.setVisible(false);
    trackFaceStatus.setVisible(false);
    mFaceMap.setVisible(false);

    scrollIndicator.setActiveId(id);

    switch (id) {
    case FaceId::ID_TRACK1: trackFaceTotal.setVisible(true);      break;
    case FaceId::ID_TRACK2: trackFaceOverview.setVisible(true);   break;
    case FaceId::ID_TRACK3: trackFaceLap.setVisible(true);        break;
    case FaceId::ID_TRACK4: trackFaceStatus.setVisible(true);     break;
    case FaceId::ID_MAP:
        mFaceMap.setVisible(true);
        updateMapFace();
        break;
    default: break;
    }

    trackFaceOverview.invalidate();
    trackFaceTotal.invalidate();
    trackFaceLap.invalidate();
    trackFaceStatus.invalidate();
    mFaceMap.invalidate();
}

uint16_t TrackView::getPositionId()
{
    return mCurrentFaceId;
}

void TrackView::setConfig(bool isImperial, const uint8_t* thresholds, uint8_t thresholdCount)
{
    mIsImperial = isImperial;
    mHrThresholdCount = thresholdCount < App::Config::kHrThresholdsCount ?
        thresholdCount : static_cast<uint8_t>(App::Config::kHrThresholdsCount);
    memcpy(mHrThresholds, thresholds, mHrThresholdCount);
}

void TrackView::setTimeFormat(bool is12Hour)
{
    mIs12Hour = is12Hour;
}

void TrackView::setTrackData(const Track::Data& data)
{
    auto distConv = [this](float metres) -> float {
        const float km = metres / 1000.0f;
        return mIsImperial ? SDK::Utils::kmToMiles(km) : km;
    };

    auto speedConv = [this](float mPerSec) -> float {
        const float kmPerHour = mPerSec * 3.6f;
        return mIsImperial ? SDK::Utils::kmToMiles(kmPerHour) : kmPerHour;
    };

    auto elevationConv = [this](float metres) -> float {
        return mIsImperial ? SDK::Utils::metersToFeet(metres) : metres;
    };

    trackFaceTotal.setSpeed(speedConv(data.speed), mIsImperial);
    trackFaceTotal.setDistance(distConv(data.distance), mIsImperial);
    trackFaceTotal.setTimer(data.totalTime);

    trackFaceOverview.setHR(data.hr, mHrThresholds, mHrThresholdCount);
    trackFaceOverview.setAvgSpeed(speedConv(data.avgSpeed), mIsImperial);
    trackFaceOverview.setElevation(elevationConv(data.elevation), mIsImperial);

    trackFaceLap.setSpeed(speedConv(data.avgLapSpeed), mIsImperial);
    trackFaceLap.setDistance(distConv(data.lapDistance), mIsImperial);
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

void TrackView::handleKeyEvent(uint8_t key)
{
    const uint16_t minId = static_cast<uint16_t>(FaceId::ID_TRACK1);

    if (key == SDK::GUI::Button::L1) {
        uint16_t p = mCurrentFaceId;
        if (p <= minId) {
            p = FaceId::ID_COUNT - 1;
        } else {
            p--;
        }
        setPositionId(p);
    }

    if (key == SDK::GUI::Button::L2) {
        uint16_t p = mCurrentFaceId + 1u;
        if (p >= FaceId::ID_COUNT) {
            p = minId;
        }
        setPositionId(p);
    }

    if (key == SDK::GUI::Button::R1) {
        application().gotoTrackActionScreenNoTransition();
    }

    if (key == SDK::GUI::Button::R2) {
        // R2 is the lap button on every stock face, and stays so. On the map
        // face -- which is new, so this takes nothing away from what the stock
        // app did -- it cycles the zoom instead: all four buttons are already
        // spoken for and zoom needs one. A lap stays one L1/L2 press away.
        if (mCurrentFaceId == FaceId::ID_MAP) {
            presenter->cycleMapZoom();
        } else {
            presenter->saveLap();
            application().gotoTrackLapScreenNoTransition();
        }
    }
}

void TrackView::setGpsFix(bool state)
{
    trackFaceStatus.setGps(SDK::Gui::SensorStatusRow::gpsState(state));
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
}

void TrackView::updateMapFace()
{
    if (mCurrentFaceId != FaceId::ID_MAP) {
        return;     // not on screen; nothing to repaint
    }
    mFaceMap.update(presenter->mapSession());
}
