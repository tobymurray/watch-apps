#include "Service.hpp"

#include <cstring>
#include <ctime>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "CardStore.hpp"

#define LOG_MODULE_PRX   "SlumberGlance"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

constexpr char     kGlanceName[]  = "Slumber";
constexpr uint32_t kSendTimeoutMs = 100;

uint32_t nowUtc()
{
    const std::time_t now = std::time(nullptr);
    return now > 0 ? static_cast<uint32_t>(now) : 0u;
}

GlanceColor_t inkFor(CardView::Tone tone)
{
    switch (tone) {
        case CardView::Tone::Warning: return GlanceColor_t::GLANCE_COLOR_YELLOW_DARK;
        case CardView::Tone::Muted:   return GlanceColor_t::GLANCE_COLOR_GRAY;
        case CardView::Tone::Normal:  break;
    }
    return GlanceColor_t::GLANCE_COLOR_WHITE;
}

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

bool Service::glanceConfig()
{
    auto gc = SDK::make_msg<SDK::Message::RequestGlanceConfig>(mKernel);
    if (!gc || !gc.send(kSendTimeoutMs) || !gc.ok() || gc->maxControls < 2) {
        return false;
    }
    mGlance.setWidth(gc->width);
    mGlance.setHeight(gc->height);
    return true;
}

void Service::glanceCreate()
{
    // The carousel's scroll indicator covers the left 18 px of a 240x60 card
    // (SunGlance/README.md, measured on the watch).
    mTop = mGlance.createText();
    mTop.pos({ 18, 2 }, { 205, 28 })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_20)
        .color(GlanceColor_t::GLANCE_COLOR_CYAN_LIGHT)
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    mBottom = mGlance.createText();
    mBottom.pos({ 18, 32 }, { 205, 26 })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18)
        .color(GlanceColor_t::GLANCE_COLOR_WHITE)
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);
}

void Service::reload()
{
    mRead = SlumberCard::load(mKernel.fs, mCard);
    if (mRead == SlumberCard::Read::Ok || mRead == SlumberCard::Read::FromBackup) {
        mLast     = mCard;
        mHaveLast = true;
    }
    LOG_INFO("card read=%d seq=%lu\n", static_cast<int>(mRead),
             static_cast<unsigned long>(mCard.seq));
}

void Service::render()
{
    const CardView::View v =
        CardView::make(mRead, mCard, mHaveLast ? &mLast : nullptr, nowUtc());

    // Setting text invalidates the form, so only a change is written.
    if (std::strcmp(v.top, mShown.top) != 0) {
        mTop.setText(v.top);
    }
    if (std::strcmp(v.bottom, mShown.bottom) != 0 || v.tone != mShown.tone) {
        mBottom.setText(v.bottom);
        mBottom.color(inkFor(v.tone));
    }
    mShown = v;
}

void Service::push()
{
    if (!mActive || !mGlance.isInvalid()) {
        return;
    }
    if (auto upd = SDK::make_msg<SDK::Message::RequestGlanceUpdate>(mKernel)) {
        upd->name           = kGlanceName;
        upd->controls       = mGlance.data();
        upd->controlsNumber = static_cast<uint32_t>(mGlance.size());
        // Marked valid only once sent (SleepLab ledger row T2).
        if (upd.send(kSendTimeoutMs)) {
            mGlance.setValid();
        }
    }
}

void Service::run()
{
    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::EVENT_GLANCE_START:
                if (!glanceConfig()) {
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }
                glanceCreate();
                mActive = true;
                reload();
                render();
                break;

            case SDK::MessageType::EVENT_GLANCE_TICK:
                if (++mTicks % kReloadEveryTicks == 0) {
                    reload();
                }
                render();
                push();
                break;

            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::EVENT_GLANCE_STOP:
                mActive = false;
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
