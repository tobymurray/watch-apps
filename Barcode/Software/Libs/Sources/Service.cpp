#include "Service.hpp"

#include <cstring>

#include "AppConfigFields.hpp"
#include "Code128.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

// The encoder's limit and the state buffer's are the same number in two
// headers, and adopt() relies on that for memory safety: it copies only after
// the encoder has accepted the value.
static_assert(Barcode::kMaxIdLength == Code128::kMaxDataLength,
              "the id buffer and the encoder must agree on the longest id");
static_assert(Barcode::kConfigMaxLength > Barcode::kMaxIdLength,
              "the declared field must be longer than an id, so over-length "
              "values arrive detectable rather than truncated into valid ones");

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mSender(kernel)
    , mState(Barcode::makeUnsetState(Barcode::Problem::NoConfig))
{
}

void Service::run()
{
    LOG_INFO("Started\n");

    // The configuration is read here and not in the constructor because
    // reading it can log, and the simulator constructs the app's objects
    // before TouchGFX exists -- its logger writes through touchgfx_printf, so
    // the first log line from a constructor segfaults the process. The SDK
    // documents this trap for SDK::AppConfig specifically; the device harness
    // has no such ordering, but there is no reason to depend on that.
    load();
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
            // notification available: nothing reports that a file written
            // from the phone or over USB has changed, so re-read here.
            load();
            adopt();
            break;

        default:
            // Not one of ours -- nothing to publish.
            return;
    }

    publish();
}

void Service::load()
{
    // Destroy the previous instance before building the next one: the class
    // is documented as one instance per app, and two of them alive at once
    // would briefly share the same temporary file.
    mConfig.reset();
    mConfig.reset(new (std::nothrow) SDK::AppConfig(
        mKernel, BarcodeConfig::kConfigFile,
        BarcodeConfig::kFields, BarcodeConfig::kFieldCount));
}

void Service::adopt()
{
    if (!mConfig || !mConfig->isLoaded()) {
        // Absent, oversized, not JSON, wrong schema, or no values object --
        // SDK::AppConfig reports all five the same way, and the detail it
        // logged is not something a wearer can go and read.
        mState = Barcode::makeUnsetState(Barcode::Problem::NoConfig);
        return;
    }

    if (!mConfig->has(Barcode::kIdField)) {
        // The key is missing, or it is present and unusable -- wrong JSON
        // type, null, or shorter than the declared minimum. has() folds those
        // together because the value in play is the default either way.
        mState = Barcode::makeUnsetState(Barcode::Problem::NoValue);
        return;
    }

    // One byte longer than an id, so a value too long to be one arrives
    // detectably over-length instead of quietly shortened. See Barcode.hpp.
    char raw[Barcode::kConfigMaxLength + 1] {};
    const size_t length = mConfig->getString(Barcode::kIdField, raw, sizeof(raw));

    if (length == 0) {
        mState = Barcode::makeUnsetState(Barcode::Problem::NoValue);
        return;
    }

    if (std::strcmp(raw, Barcode::kUnsetId) == 0) {
        // The declared default came back. Either nobody has set an id, or the
        // phone's form was accepted with the field left as it was found --
        // which the SDK counts as satisfying a required field. Both mean the
        // same thing here: there is no id, and drawing one would be a lie.
        mState = Barcode::makeUnsetState(Barcode::Problem::NotSet);
        return;
    }

    // Checked before the encoder rather than left to it: the encoder would
    // refuse an over-length value too, but this is what makes the copy below
    // provably fit, without depending on two headers agreeing.
    if (length > Barcode::kMaxIdLength) {
        mState = Barcode::makeUnsetState(Barcode::Problem::BadValue);
        return;
    }

    // The encoder is the remaining validator, and it is the right one: it
    // refuses anything outside printable ASCII, which is exactly the set this
    // app can draw. SDK::AppConfig decodes JSON escapes before this point, so
    // a `\\` in the file arrives as a real backslash and encodes as one --
    // the old hand-rolled reader had to refuse it, because it saw the escape
    // sequence undecoded.
    Barcode::State next = Barcode::makeUnsetState(Barcode::Problem::None);
    Code128::Encoded probe {};
    if (!Code128::encode(raw, probe)) {
        mState = Barcode::makeUnsetState(Barcode::Problem::BadValue);
        return;
    }

    std::memcpy(next.id, raw, length + 1);

    LOG_INFO("Using id from %s\n", BarcodeConfig::kConfigFile);
    mState = next;
}

void Service::publish()
{
    mSender.state(mState);
}
