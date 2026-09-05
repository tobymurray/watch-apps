#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Barcode.hpp"
#include "barcode_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void cycle(int delta);
    void buildFrame(barcode_gui_frame &out) const;

    uint8_t lastIndex() const;
    void    rememberIndex(uint8_t index);

    /// The code on screen encodes finer than the panel draws cleanly --
    /// BarcodeLayout::Scannability::modulesAreAtLeastOnePixel() has the
    /// measurement and what it means.
    bool isDense() const;

    /// Dense, and the wearer has not yet said they want to see it anyway.
    bool warningShowing() const;

    void acknowledge();
    bool acknowledged(const char *id) const;
    void loadAcknowledgements();
    void saveAcknowledgements();

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

    Barcode::State mState;
    uint8_t        mIndex       = 0;
    bool           mIndexLoaded = false;

    /// Ids whose dense-code warning the wearer has already dismissed, keyed by
    /// the id itself rather than by slot: a slot re-pointed at a different
    /// code is a different barcode and has not been acknowledged. Holds at
    /// most one per slot, which is as many distinct ids as can be on the watch.
    char    mAckIds[Barcode::kMaxCodes][Barcode::kMaxIdLength + 1] = {};
    uint8_t mAckCount   = 0;
    bool    mAcksLoaded = false;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
