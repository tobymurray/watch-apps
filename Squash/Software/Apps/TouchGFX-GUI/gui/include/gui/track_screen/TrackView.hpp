#ifndef TRACKVIEW_HPP
#define TRACKVIEW_HPP

#include <gui_generated/track_screen/TrackViewBase.hpp>
#include <gui/track_screen/TrackPresenter.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class TrackView : public TrackViewBase
{
public:
    TrackView();
    virtual ~TrackView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    void setConfig(bool isImperial, const uint8_t* thresholds, uint8_t thresholdCount);
    void setTimeFormat(bool is12Hour);
    void setTrackData(const Track::Data& data);

    void setTime(uint8_t h, uint8_t m);
    void setBatteryLevel(uint8_t level);
    void setAccessoryStatus(uint8_t state);

    /**
     * @brief Say, on the glass, that the strap stopped feeding heart rate.
     * @param secondsWithout How long external HR has been absent.
     *
     * Shown on the track screen rather than a face, so it appears whichever
     * face the wearer is on. It times itself out: an alert that has to be
     * dismissed is an alert that interrupts a session, and this one is
     * information the wearer can act on later or not at all.
     */
    void showHrStrapLost(uint16_t secondsWithout);

protected:

    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

    // In-activity HR icon reflects the live source, recomputed from both the
    // accessory link state (engaged?) and the latest HR-sample source.
    void updateHrIcon();

    uint16_t mCurrentFaceId = 0;
    bool     mIsImperial    = false;
    bool     mIs12Hour      = false;
    uint8_t  mHrThresholds[App::Config::kHrThresholdsCount] = {};
    uint8_t  mHrThresholdCount = 0;
    uint8_t  mAccessoryState = 0;  // last SDK::Accessory::State (engaged?)
    uint8_t  mHrSource       = 0;  // last HR-sample source (steady vs flashing)

    /// Ticks the banner stays up. 10 fps, so four seconds -- long enough to
    /// read mid-session, short enough not to hide a face's numbers.
    static const int16_t kBannerTicks = 40;

    /// Inset from the 240 px square so both ends stay inside the round bezel.
    static const int16_t kBannerX = 20;
    static const int16_t kBannerY = 96;
    static const int16_t kBannerW = 200;
    static const int16_t kBannerH = 26;

    touchgfx::Box                     mBannerBg;
    touchgfx::TextAreaWithOneWildcard mBanner;
    touchgfx::Unicode::UnicodeChar    mBannerBuffer[24];
    int16_t                           mBannerTicksLeft = 0;
};

#endif // TRACKVIEW_HPP
