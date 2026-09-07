#ifndef GUI_HPP
#define GUI_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "DebugLog.hpp"
#include "SettingsAddresses.hpp"
#include "unit_toggle_gui.h"

/// The real value comes from `BUILD_VERSION` via CMake; this keeps the
/// host type-check in `README.md` compiling without it.
#ifndef UNIT_TOGGLE_VERSION
#define UNIT_TOGGLE_VERSION "unknown"
#endif

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    bool resolveFirmwareSupport();
    void renderAndPush();
    void refreshLiveState();
    void toggle();

    /// The units as the kernel itself reports them, through the one supported
    /// message that carries them. False if the kernel would not answer.
    bool readKernelUnits(bool &outImperial);

    /// True if the raw byte matches what the kernel has just reported, which is
    /// what proves the address is the units field rather than some byte that
    /// merely accepted a write. Takes the report rather than asking again, so
    /// the value checked is the value drawn.
    bool rawAgreesWithKernel(bool kernelImperial);

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;
    static constexpr uint8_t  kMaxBitsPerPixel   = 8;

    // The GUI ticks at ~10 fps (RustGuiPoc's Docs/FINDINGS.md), so this polls
    // about once a second rather than every tick -- enough to notice the phone
    // app changing the same setting while this screen is open.
    static constexpr uint32_t kReReadEveryTicks = 10;

    SDK::Kernel &mKernel;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;
    bool     mResumed    = false;
    // The renderer writes one byte per pixel; a panel wanting more would have
    // the kernel read past mFrameBuf, so frames are withheld instead.
    bool     mDisplayUsable = true;
    uint32_t mTicksSinceRead = 0;

    /// What the last R1 press left behind. Sticky until the next press: a
    /// press that failed is invisible to a fresh read -- the revert puts the
    /// byte back, so the read agrees again -- and without this the screen
    /// returns to a confident answer about a change that did not happen.
    enum class PressOutcome {
        Clean,
        Unreliable,     ///< The live byte could not be read, written, or witnessed.
        NotPersisted,   ///< Live change took effect; settings.json was not written.
    };
    PressOutcome mLastPress = PressOutcome::Clean;

    /// What the kernel reported when `mLastPress` was set. A `NotPersisted`
    /// outcome describes one value; once something else has changed the units,
    /// it no longer describes what is on screen and is dropped.
    uint8_t mLastPressValue = 0;

    // The wearer's answer to "also write this to the watch's settings file".
    // False until the config says otherwise, so an install that nobody
    // configures never writes anything.
    bool mSaveToSettings = false;

    bool mPrimitivesChecked = false;
    bool mPrimitivesOk      = false;

    unit_toggle_state mState{};

    // Resolved once at startup from the watch's actual running firmware
    // version. Null on any firmware this app hasn't been reverse-engineered and
    // cross-validated against -- every LiveSettings/SettingsPersist call site is
    // gated on this being non-null, never called with a stale or default set.
    const SettingsAddresses::AddressSet *mAddresses = nullptr;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
