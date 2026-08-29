#include "Service.hpp"

#include <cstring>

#include "AppConfigFields.hpp"
#include "Symbology.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

// The encoder's limit and the id buffer's are the same number in two headers,
// and adopt() relies on that for memory safety: it copies only after the
// encoder has accepted the value.
static_assert(Barcode::kMaxIdLength == Code128::kMaxDataLength,
              "the id buffer and the encoder must agree on the longest id");
// Not equality, because QR's capacity is a property of the version this app
// fixed rather than of the id limit. What matters is that it is not smaller:
// it is what makes adding QR add no new way for a code to be refused, and it
// is why Problem::BadValue's prompt is still true as written.
static_assert(Qr::kMaxDataLength >= Barcode::kMaxIdLength,
              "the QR version must carry the longest id this app accepts");
static_assert(Barcode::kConfigMaxLength > Barcode::kMaxIdLength,
              "the declared field must be longer than an id, so over-length "
              "values arrive detectable rather than truncated into valid ones");

namespace {

/// Everything the screen font has a glyph for. Applied to names, which no
/// encoder ever sees -- a name is decoration and is not in any barcode.
bool isPlainAscii(const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < 32 || text[i] > 126) {
            return false;
        }
    }
    return true;
}

} // namespace

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

Service::Adopted Service::adoptCode(size_t index, Barcode::Code &out) const
{
    // One byte longer than an id, so a value too long to be one arrives
    // detectably over-length instead of quietly shortened. See Barcode.hpp.
    char raw[Barcode::kConfigMaxLength + 1] {};
    const size_t length =
        mConfig->getString(BarcodeConfig::idField(index), raw, sizeof(raw));

    if (length == 0) {
        // An empty id is how a slot says it is unused. Every field defaults to
        // the empty string, so this is also what an untouched slot reads as.
        return Adopted::Empty;
    }

    // Checked before the encoder rather than left to it: the encoder would
    // refuse an over-length value too, but this is what makes the copy below
    // provably fit, without depending on two headers agreeing.
    if (length > Barcode::kMaxIdLength) {
        LOG_WARNING("%s is %u bytes, too long for an id\n",
                    BarcodeConfig::idField(index), static_cast<unsigned>(length));
        return Adopted::BadValue;
    }

    // Which symbology to draw it as: "Code128" or "QRCode", either case. The
    // declared default is the literal "Code128", so a key missing from the file
    // reads as that and an input.json written before this field existed keeps
    // meaning what it meant -- see Barcode::parseFormat().
    char formatText[Barcode::kConfigFormatMaxLength + 1] {};
    mConfig->getString(BarcodeConfig::formatField(index), formatText, sizeof(formatText));

    Barcode::Format format = Barcode::Format::Code128;
    if (!Barcode::parseFormat(formatText, format)) {
        // Refused rather than quietly drawn as Code 128. The value would still
        // be the wearer's own id, so this is not the harm Barcode.hpp is about
        // -- but "drew something other than what you asked for, silently" is
        // not a thing this app does, and the phone's pattern means only a
        // hand-edited file can get here.
        LOG_WARNING("%s is not a format this app draws\n",
                    BarcodeConfig::formatField(index));
        return Adopted::BadFormat;
    }

    // The encoder is the remaining validator, and it is the right one: it
    // refuses anything the chosen format cannot carry, which for Code 128 is
    // anything outside printable ASCII -- exactly the set this app can draw.
    // SDK::AppConfig decodes JSON escapes before this point, so a `\\` in the
    // file arrives as a real backslash and encodes as one.
    if (!Barcode::isDrawable(format, raw)) {
        switch (Barcode::refusalFor(format, raw)) {
        case Barcode::Refusal::BadDigitCount:
            LOG_WARNING("%s has the wrong number of digits\n", BarcodeConfig::idField(index));
            return Adopted::BadDigitCount;
        case Barcode::Refusal::BadCharacters:
            LOG_WARNING("%s has a character its format cannot draw\n", BarcodeConfig::idField(index));
            return Adopted::BadCharacters;
        case Barcode::Refusal::BadValue:
            break;
        }
        LOG_WARNING("%s cannot be drawn\n", BarcodeConfig::idField(index));
        return Adopted::BadValue;
    }

    std::memcpy(out.id, raw, length + 1);
    out.format = format;

    // The name is decoration: a bad one costs the label, never the code.
    char name[Barcode::kMaxNameLength + 1] {};
    const size_t nameLength =
        mConfig->getString(BarcodeConfig::nameField(index), name, sizeof(name));
    if (nameLength > 0 && isPlainAscii(name)) {
        std::memcpy(out.name, name, nameLength + 1);
    }

    return Adopted::Yes;
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

    Barcode::State next {};
    next.problem = Barcode::Problem::None;

    bool anyKeyPresent    = false;  // a slot carried a value, even an empty one
    bool anyBadValue      = false;  // a slot carried an id that cannot be drawn
    bool anyBadFormat     = false;  // a slot named a format this app does not draw
    bool anyBadDigitCount = false;  // a digit-only format's id was the wrong length
    bool anyBadCharacters = false;  // a digit-only format's id held a non-digit

    for (size_t i = 0; i < Barcode::kMaxCodes; i++) {
        if (mConfig->has(BarcodeConfig::idField(i))) {
            anyKeyPresent = true;
        }

        Barcode::Code code {};
        switch (adoptCode(i, code)) {
        case Adopted::Yes:
            next.codes[next.count] = code;
            next.count++;
            break;
        case Adopted::BadValue:
            anyBadValue = true;
            break;
        case Adopted::BadFormat:
            anyBadFormat = true;
            break;
        case Adopted::BadDigitCount:
            anyBadDigitCount = true;
            break;
        case Adopted::BadCharacters:
            anyBadCharacters = true;
            break;
        case Adopted::Empty:
            break;
        }
    }

    if (next.count > 0) {
        // A bad slot alongside good ones is skipped rather than announced: the
        // phone validates before it writes, so one bad slot means a hand-edited
        // file, and hiding the codes that work would be the wrong trade.
        LOG_INFO("Using %u code(s) from %s\n",
                 static_cast<unsigned>(next.count), BarcodeConfig::kConfigFile);
        mState = next;
        return;
    }

    if (anyBadCharacters) {
        // The two most specific reasons outrank the generic one when both are
        // present, for the same reason BadValue outranks BadFormat below: a
        // wearer benefits more from the fault the prompt can actually name.
        mState = Barcode::makeUnsetState(Barcode::Problem::BadCharacters);
    } else if (anyBadDigitCount) {
        mState = Barcode::makeUnsetState(Barcode::Problem::BadDigitCount);
    } else if (anyBadValue) {
        // Reported ahead of a bad format when both are present: an id that
        // cannot be drawn is the fault the wearer is more likely to have made
        // and the one the prompt can actually be specific about.
        mState = Barcode::makeUnsetState(Barcode::Problem::BadValue);
    } else if (anyBadFormat) {
        mState = Barcode::makeUnsetState(Barcode::Problem::BadFormat);
    } else if (anyKeyPresent) {
        // The form was filled in and left empty, which is what accepting a
        // required field's pre-filled default looks like from here.
        mState = Barcode::makeUnsetState(Barcode::Problem::NotSet);
    } else {
        mState = Barcode::makeUnsetState(Barcode::Problem::NoValue);
    }
}

void Service::publish()
{
    mSender.state(mState);
}
