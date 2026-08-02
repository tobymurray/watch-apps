#include "Service.hpp"

#include "Code128.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mSender(kernel)
    , mInput(kernel)
    , mState(Barcode::makeUnsetState(Barcode::Problem::NoFile))
{
}

void Service::run()
{
    LOG_INFO("Started\n");

    // The file is read here and not in the constructor because reading it can
    // log, and the simulator constructs the app's objects before TouchGFX
    // exists -- its logger writes through touchgfx_printf, so the first log
    // line from a constructor segfaults the process. The device harness has no
    // such ordering, but there is no reason to depend on that.
    mInput.refresh();
    adopt();

    while (true) {
        SDK::MessageBase *msg;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                LOG_INFO("Force exit from the application\n");
                // We must release message because this is the last event.
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                LOG_INFO("GUI is now running\n");
                publish();
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("GUI has stopped\n");
                // Nothing here changes on its own, so unlike Stopwatch there
                // is no reason to keep the thread resident once the GUI is
                // gone.
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                handleCommand(msg);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}

void Service::handleCommand(SDK::MessageBase *msg)
{
    switch (msg->getType()) {
        case CustomMessage::BARCODE_REQUEST:
            // The GUI asks on every resume. That is the only change
            // notification available: no message type reports that a file
            // written from outside the watch has changed, so re-check here.
            // Costs one stat when nothing has moved.
            if (mInput.refresh()) {
                adopt();
            }
            break;

        default:
            // Not one of ours -- nothing to publish.
            return;
    }

    publish();
}

void Service::adopt()
{
    switch (mInput.status()) {
        case InputConfig::Status::Absent:
            mState = Barcode::makeUnsetState(Barcode::Problem::NoFile);
            return;
        case InputConfig::Status::TooLarge:
            mState = Barcode::makeUnsetState(Barcode::Problem::TooLarge);
            return;
        case InputConfig::Status::Unreadable:
            mState = Barcode::makeUnsetState(Barcode::Problem::Unreadable);
            return;
        case InputConfig::Status::NotJson:
            mState = Barcode::makeUnsetState(Barcode::Problem::NotJson);
            return;
        case InputConfig::Status::WrongSchema:
            mState = Barcode::makeUnsetState(Barcode::Problem::WrongSchema);
            return;
        case InputConfig::Status::Ok:
            break;
    }

    Barcode::State next = Barcode::makeUnsetState(Barcode::Problem::None);

    if (!mInput.getString(Barcode::kIdQuery, next.id, sizeof(next.id))) {
        mState = Barcode::makeUnsetState(mInput.has(Barcode::kIdQuery)
                                             ? Barcode::Problem::BadValue
                                             : Barcode::Problem::NoKey);
        return;
    }

    // The reader bounds and screens the value, but the encoder is what
    // decides whether it can be drawn. Asking it here keeps the two sets of
    // limits from drifting apart into a blank white box with nothing said
    // about why.
    Code128::Encoded probe {};
    if (!Code128::encode(next.id, probe)) {
        mState = Barcode::makeUnsetState(Barcode::Problem::BadValue);
        return;
    }

    LOG_INFO("Using id from %s\n", InputConfig::kPath);
    mState = next;
}

void Service::publish()
{
    mSender.state(mState);
}
