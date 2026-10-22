#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Glance/GlanceControl.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "CardView.hpp"
#include "SlumberCard.hpp"

/// The carousel starts this service when the card scrolls into view and stops
/// it when the card leaves, so it holds nothing worth keeping between views.
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service() = default;

    void run();

private:
    bool glanceConfig();
    void glanceCreate();
    void reload();
    void render();
    void push();

    /// The card stays readable while it is on screen without a stat per tick.
    static constexpr uint32_t kReloadEveryTicks = 30;

    SDK::Kernel &mKernel;

    SlumberCard::Card mCard;
    SlumberCard::Card mLast;
    bool              mHaveLast = false;
    SlumberCard::Read mRead     = SlumberCard::Read::Absent;
    uint32_t          mTicks    = 0;

    SDK::Glance::Form        mGlance;
    SDK::Glance::ControlText mTop;
    SDK::Glance::ControlText mBottom;
    CardView::View           mShown;
    bool                     mActive = false;
};

#endif // SERVICE_HPP
