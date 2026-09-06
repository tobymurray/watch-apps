#ifndef GUI_HPP
#define GUI_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "DebugLog.hpp"
#include "FirmwareGate.hpp"
#include "SettingsAddresses.hpp"
#include "unit_toggle_gui.h"

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

    /// True once the raw byte and the kernel's own report have been seen to
    /// agree, which is what proves the address is the units field rather than
    /// some byte that merely accepted a write.
    bool rawAgreesWithKernel();

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

    // Sticky until the next R1, so the periodic re-read cannot quietly turn a
    // change that never reached the file back into a confident answer.
    bool mPersistFailed = false;

    // The wearer's answer to "also write this to the watch's settings file".
    // False until the config says otherwise, so an install that nobody
    // configures never writes anything.
    bool mSaveToSettings = false;

    FirmwareGate::Outcome mGateOutcome = FirmwareGate::Outcome::UnknownFirmware;

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
