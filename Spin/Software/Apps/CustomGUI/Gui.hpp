#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>
#include <ctime>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "Commands.hpp"
#include "Track.hpp"
#include "spin_gui.h"

/// The GUI process: a message loop, a framebuffer, and a translation from the
/// Service's snapshots into one `spin_gui_frame`. It owns no clock and no
/// sensor -- see Commands.hpp. Every pixel is drawn by the Rust crate under
/// `rust/`, which is handed the frame and nothing else.
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

    /// From the app's configuration, via the Service. 0 means no target, which
    /// is the default and what an app with no config file on the watch shows.
    uint16_t mTargetMinutes = 0;

    /// Display unit for energy. The Service always sends kcal; this decides
    /// what the screen turns it into.
    bool mEnergyInKilojoules = false;

    /// A finished ride is not a state the Service tracks -- it goes back to
    /// INACTIVE, which is also what "never started" looks like. This is the
    /// one bit that tells those two apart, set by RIDE_SAVED and cleared when
    /// the next ride starts.
    bool        mShowSaved    = false;
    std::time_t mSavedSeconds = 0;
    float       mSavedAvgHr   = 0.0f;
    float       mSavedCalories = 0.0f;   ///< kcal, whatever the display unit
    bool        mSavedOk      = false;
    bool        mDiscarded    = false;

    /// Hold-to-confirm state for Discard. The kernel decides when the hold is
    /// long enough (its HOLD_1S event); the tick count only drives the ring, so
    /// nothing here has to agree with the kernel about how long a second is.
    bool    mHoldingDiscard = false;
    uint8_t mHoldTicks      = 0;

    /// SDK::GUI::Config::kFrameRate is 10, so ten ticks is the second the
    /// kernel is timing. Used for the fill only -- if the two ever disagree the
    /// ring sits full for a moment, which is a cosmetic error, not a wrong act.
    static constexpr uint8_t kHoldTicksForFull = 10;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
