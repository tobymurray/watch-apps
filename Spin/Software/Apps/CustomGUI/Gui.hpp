#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>
#include <ctime>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "Commands.hpp"
#include "Track.hpp"
#include "spin_gui.h"

/// A message loop, a framebuffer, and a translation from the Service's
/// snapshots into one `spin_gui_frame`; every pixel is drawn by the crate under
/// `rust/`. Owns no clock and no sensor -- see Commands.hpp.
class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void buildFrame(spin_gui_frame &out) const;
    void handleButton(SDK::Message::EventButton::Id id,
                      SDK::Message::EventButton::Event event);

    static uint8_t strapFromAccessoryState(uint8_t accessoryState);

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    SDK::Kernel &mKernel;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;
    bool     mResumed    = false;

    Track::State mTrackState = Track::State::INACTIVE;
    Track::Data  mTrackData{};
    uint8_t      mStrap = SPIN_GUI_STRAP_ABSENT;

    uint16_t mTargetMinutes = 0;      ///< minutes; 0 = no target

    /// Display unit only; the Service always sends kcal.
    bool mEnergyInKilojoules = false;

    uint8_t mZoneCount = 0;           ///< dial segments; 0 = no zones set

    /// The Service has no "finished" state -- it returns to INACTIVE, which is
    /// also what "never started" looks like. This is the bit that tells them
    /// apart.
    bool        mShowSaved    = false;
    std::time_t mSavedSeconds = 0;
    float       mSavedAvgHr   = 0.0f;
    float       mSavedCalories = 0.0f;   ///< kcal, whatever the display unit
    bool        mSavedOk      = false;
    bool        mDiscarded    = false;

    /// Both questions are dropped when the GUI is suspended: one the wearer
    /// walked away from is not one they answered, and the ride underneath is
    /// still PAUSED and unsaved either way.
    bool mConfirmingDiscard = false;
    bool mEnteringWork = false;

    /// kJ; 0 = nothing said. The only entry state anywhere -- what each button
    /// does to it lives in work.rs, as pure functions of this number.
    uint16_t mWorkKilojoules = 0;

    bool mAskForKilojoules = true;    ///< from the app's configuration

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
