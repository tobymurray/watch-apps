#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "Commands.hpp"
#include "Session.hpp"
#include "squash_gui.h"

/// A message loop, a framebuffer, and a translation from the Service's status
/// snapshots into one `squash_gui_frame`; every pixel is drawn by the crate
/// under `rust/`. Owns no clock and no sensor -- see Commands.hpp.
class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void buildFrame(squash_gui_frame &out) const;
    void handleButton(SDK::Message::EventButton::Id id,
                      SDK::Message::EventButton::Event event);

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    SDK::Kernel          &mKernel;
    CustomMessage::Sender mSender;

    int16_t mWidth      = 0;
    int16_t mHeight     = 0;
    uint8_t mColorDepth = 8;
    bool    mResumed    = false;

    /// The last snapshot the Service sent, copied straight through.
    Session::Status mStatus{};
    Session::State  mState = Session::State::INACTIVE;

    /// The Service returns to INACTIVE when a session ends, which is also what
    /// "never started" looks like. This is the bit that tells them apart.
    bool mShowEnded  = false;
    bool mDiscarded  = false;
    bool mSavedOk    = false;

    /// Open only while the picker is up. The Service owns the label that is
    /// actually being written; this is only what the wearer is scrolling past.
    bool    mPicking   = false;
    uint8_t mLabelPick = SQUASH_GUI_LABEL_RALLY;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
