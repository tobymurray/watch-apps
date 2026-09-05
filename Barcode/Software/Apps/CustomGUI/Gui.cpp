#include "Gui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BarcodeLayout.hpp"
#include "Commands.hpp"
#include "Symbology.hpp"

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"

#include "barcode_gui.h"

#define LOG_MODULE_PRX   "BarcodeGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
constexpr uint32_t kWaitForever = 0xFFFFFFFF;

// Not input.json and not settings.json: the phone never writes here and
// never reads it either. Same path and format the TouchGFX GUI used --
// carried over so an app updated in place keeps its wearer's last position.
constexpr char kLastIndexPath[] = "/last_code.txt";

// One acknowledged id per line. Beside kLastIndexPath and for the same
// reason: it is this GUI's own note to itself about what the wearer has
// already been told, and no other process reads or writes it.
constexpr char kAckPath[] = "/dense_ack.txt";

/// Shown instead of a barcode the panel cannot draw cleanly, once per id.
///
/// Names the button because there is nothing else on the screen to suggest
/// the warning can be got past, and a wearer who cannot get past it has lost
/// the code rather than been warned about it. Says "data" rather than
/// anything about modules or pixels: what the wearer can act on is putting
/// less in the field, and the count that actually decides it is not one they
/// can see.
constexpr char kDenseMessage[] =
    "Too much data for this display. May not scan. R1 shows it anyway.";

/// One flat, unwrapped string per Problem; Rust word-wraps it at the chosen
/// font (see lib.rs's word_wrap()). The three that quote a limit are built
/// from the constant that sets it, for the reason BadFormat has been built
/// from Symbology::kFormatNames since a past fix: a prompt that restates a
/// number owned elsewhere goes stale the first time the number moves, silently
/// and only on the watch. The rest are static content with no data source to
/// drift from, so they are literals.
const char *promptMessage(Barcode::Problem problem, char (&scratch)[96])
{
    using Barcode::Problem;
    switch (problem) {
    case Problem::NoValue:
        return "input.json has no usable code";
    case Problem::NotSet:
        return "No codes set yet. Open the UNA app and enter your ID";
    case Problem::BadValue:
        std::snprintf(scratch, sizeof(scratch),
                      "That ID cannot be drawn: 1-%zu plain characters", Barcode::kMaxIdLength);
        return scratch;
    case Problem::BadDigitCount:
        std::snprintf(scratch, sizeof(scratch),
                      "ITF needs an even count of digits, 2 to %zu", Itf::kMaxDataLength);
        return scratch;
    case Problem::BadCharacters:
        return "ITF only draws digits 0-9";
    case Problem::BadWhitespace:
        return "That ID starts or ends with a space, remove it";
    case Problem::BadFormat: {
        int pos = std::snprintf(scratch, sizeof(scratch), "Unknown format. Set it to ");
        for (uint8_t i = 0; i < Barcode::kFormatCount && pos > 0 &&
                             static_cast<size_t>(pos) < sizeof(scratch); i++) {
            const char *sep = (i == 0) ? "" : ((i + 1 == Barcode::kFormatCount) ? " or " : ", ");
            pos += std::snprintf(scratch + pos, sizeof(scratch) - static_cast<size_t>(pos),
                                  "%s%s", sep, Barcode::kFormatNames[i]);
        }
        if (pos > 0 && static_cast<size_t>(pos) < sizeof(scratch)) {
            std::snprintf(scratch + pos, sizeof(scratch) - static_cast<size_t>(pos), ".");
        }
        return scratch;
    }
    case Problem::None:
    case Problem::NoConfig:
    default:
        return "No codes yet. Set one in the UNA app, or write input.json.";
    }
}

} // namespace

extern "C" void barcode_gui_host_panic(const uint8_t *msg, uint32_t len)
{
    LOG_ERROR("Rust panic: %.*s\n", static_cast<int>(len),
              reinterpret_cast<const char *>(msg));
    SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);
    while (true) {
    }
}

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mState(Barcode::makeUnsetState(Barcode::Problem::NoConfig))
{
}

uint8_t Gui::lastIndex() const
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLastIndexPath);
    if (!file || !file->open()) {
        return 0;
    }

    char buf[4] = {};
    size_t read = 0;
    const bool ok = file->read(buf, sizeof(buf) - 1, read);
    file->close();
    if (!ok || read == 0) {
        return 0;
    }
    buf[read] = '\0';

    const long value = std::strtol(buf, nullptr, 10);
    return (value >= 0 && value < static_cast<long>(Barcode::kMaxCodes))
               ? static_cast<uint8_t>(value)
               : 0;
}

void Gui::rememberIndex(uint8_t index)
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLastIndexPath);
    if (!file || !file->open(true, true)) {
        return;
    }

    char buf[4];
    const int len = std::snprintf(buf, sizeof(buf), "%u", index);
    if (len > 0) {
        size_t written = 0;
        file->write(buf, static_cast<size_t>(len), written);
    }
    file->close();
}

bool Gui::isDense() const
{
    if (mState.problem != Barcode::Problem::None || mIndex >= mState.count) {
        return false;
    }

    const Barcode::Code &code = mState.codes[mIndex];
    // Only the stretched-to-fit path can produce a module narrower than a
    // pixel. ITF is drawn whole-pixel, so its elements are never thinner than
    // one -- its own ceiling is Itf::kMaxDataLength, enforced by refusing.
    if (Barcode::isMatrix(code.format) ||
        Barcode::renderStyle(code.format) != Barcode::Render::Scaled) {
        return false;
    }

    Barcode::Encoded encoded {};
    if (!Barcode::encode(code.format, code.id, encoded)) {
        return false;
    }
    return !BarcodeLayout::scannabilityFor(encoded.totalModules).modulesAreAtLeastOnePixel();
}

bool Gui::warningShowing() const
{
    return isDense() && !acknowledged(mState.codes[mIndex].id);
}

bool Gui::acknowledged(const char *id) const
{
    for (uint8_t i = 0; i < mAckCount; i++) {
        if (std::strncmp(mAckIds[i], id, sizeof(mAckIds[i])) == 0) {
            return true;
        }
    }
    return false;
}

void Gui::acknowledge()
{
    const char *id = mState.codes[mIndex].id;
    if (acknowledged(id)) {
        return;
    }
    // Full means every slot on the watch holds a distinct acknowledged id, so
    // the one being added must already be replacing a code that is gone.
    // Dropping the oldest is the only choice that keeps this bounded, and it
    // costs at worst one repeat of a warning already dismissed.
    if (mAckCount == Barcode::kMaxCodes) {
        for (uint8_t i = 1; i < mAckCount; i++) {
            std::memcpy(mAckIds[i - 1], mAckIds[i], sizeof(mAckIds[i]));
        }
        mAckCount--;
    }
    std::strncpy(mAckIds[mAckCount], id, sizeof(mAckIds[mAckCount]) - 1);
    mAckIds[mAckCount][sizeof(mAckIds[mAckCount]) - 1] = '\0';
    mAckCount++;
    saveAcknowledgements();
}

void Gui::loadAcknowledgements()
{
    mAckCount = 0;

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kAckPath);
    if (!file || !file->open()) {
        return;
    }

    char   buf[Barcode::kMaxCodes * (Barcode::kMaxIdLength + 1) + 1] = {};
    size_t read = 0;
    const bool ok = file->read(buf, sizeof(buf) - 1, read);
    file->close();
    if (!ok || read == 0) {
        return;
    }
    buf[read] = '\0';

    for (char *line = buf; *line != '\0' && mAckCount < Barcode::kMaxCodes;) {
        char *end = std::strchr(line, '\n');
        if (end != nullptr) {
            *end = '\0';
        }
        if (*line != '\0') {
            std::strncpy(mAckIds[mAckCount], line, sizeof(mAckIds[mAckCount]) - 1);
            mAckIds[mAckCount][sizeof(mAckIds[mAckCount]) - 1] = '\0';
            mAckCount++;
        }
        if (end == nullptr) {
            break;
        }
        line = end + 1;
    }
}

void Gui::saveAcknowledgements()
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kAckPath);
    if (!file || !file->open(true, true)) {
        return;
    }

    for (uint8_t i = 0; i < mAckCount; i++) {
        char line[Barcode::kMaxIdLength + 2];
        const int len = std::snprintf(line, sizeof(line), "%s\n", mAckIds[i]);
        if (len > 0) {
            size_t written = 0;
            file->write(line, static_cast<size_t>(len), written);
        }
    }
    file->close();
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg) {
        if (mKernel.comm.sendMessage(cfg, kResponseTimeoutMs) &&
            cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth      = cfg->width;
            mHeight     = cfg->height;
            mColorDepth = cfg->colorDepth;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    const bool fitsFramebuffer =
        mWidth > 0 && mHeight > 0 &&
        static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) <= kMaxPixels;

    if (!fitsFramebuffer) {
        LOG_WARNING("Display config unusable (%dx%d); falling back to %dx%d\n",
                    mWidth, mHeight, kFallbackWidth, kFallbackHeight);
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

void Gui::cycle(int delta)
{
    // No-op unless there is something to cycle between -- matches the
    // original MainView::cycle()'s guard exactly.
    if (mState.problem != Barcode::Problem::None || mState.count < 2) {
        return;
    }
    const int count = mState.count;
    mIndex = static_cast<uint8_t>(((static_cast<int>(mIndex) + delta) % count + count) % count);
    rememberIndex(mIndex);
}

void Gui::buildFrame(barcode_gui_frame &out) const
{
    std::memset(&out, 0, sizeof(out));

    if (mState.problem != Barcode::Problem::None) {
        out.kind = BARCODE_GUI_KIND_PROMPT;
        char scratch[sizeof(out.message)];
        const char *message = promptMessage(mState.problem, scratch);
        std::strncpy(out.message, message, sizeof(out.message) - 1);
        return;
    }

    // Stands in front of the barcode rather than replacing it: the wearer
    // dismisses this once per id and never sees it for that id again.
    if (warningShowing()) {
        out.kind = BARCODE_GUI_KIND_PROMPT;
        std::strncpy(out.message, kDenseMessage, sizeof(out.message) - 1);
        return;
    }

    const Barcode::Code &code = mState.codes[mIndex];
    std::strncpy(out.id, code.id, sizeof(out.id) - 1);
    std::strncpy(out.name, code.name, sizeof(out.name) - 1);
    out.index = mIndex;
    out.count = mState.count;

    if (Barcode::isMatrix(code.format)) {
        out.kind = BARCODE_GUI_KIND_QR;
        Barcode::Matrix matrix{};
        if (Barcode::encode(code.format, code.id, matrix)) {
            static_assert(sizeof(matrix.bits) == sizeof(out.matrix_bits),
                          "Barcode::Matrix and barcode_gui_frame have diverged");
            std::memcpy(out.matrix_bits, matrix.bits, sizeof(out.matrix_bits));
            out.matrix_size = matrix.size;
        }
        return;
    }

    out.kind = (code.format == Barcode::Format::Itf) ? BARCODE_GUI_KIND_ITF
                                                       : BARCODE_GUI_KIND_CODE128;
    Barcode::Encoded encoded{};
    if (Barcode::encode(code.format, code.id, encoded)) {
        static_assert(sizeof(encoded.widths) <= sizeof(out.widths),
                      "Barcode::Encoded and barcode_gui_frame have diverged");
        std::memcpy(out.widths, encoded.widths, sizeof(encoded.widths));
        out.width_count    = encoded.count;
        out.total_modules  = encoded.totalModules;
    }
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    barcode_gui_frame frame;
    buildFrame(frame);

    barcode_gui_render(mFrameBuf, kMaxPixels * kBytesPerPixel,
                        static_cast<uint16_t>(mWidth), static_cast<uint16_t>(mHeight),
                        &frame);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");

    if (barcode_gui_abi_fingerprint() != barcode_gui_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libbarcode_gui.a\n",
                  static_cast<unsigned>(barcode_gui_abi_fingerprint()),
                  static_cast<unsigned>(barcode_gui_abi::fingerprint()));
        mKernel.sys.exit(1);
        return;
    }

    queryDisplayConfig();

    CustomMessage::Sender sender(mKernel);
    // The service publishes a snapshot when it is told the GUI is up, so this
    // is only a safety net for the case where that message beat this loop
    // being ready -- same reasoning as the TouchGFX GUI's Model::onStart().
    sender.requestState();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::COMMAND_APP_STOP:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0);
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                mResumed = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                // Nothing tells this app that input.json was rewritten, not
                // from the phone and not over USB -- asking on every resume
                // is the notification. The reply arrives as BARCODE_STATE
                // below and triggers the actual redraw.
                sender.requestState();
                continue;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::BARCODE_STATE: {
                mState = static_cast<CustomMessage::BarcodeState *>(msg)->state;

                // Only the first snapshot with an actual code in it loads the
                // saved index -- not merely the first call. The constructor's
                // placeholder state has count 0, and loading against that
                // would consume the one-time load on nothing to show and then
                // immediately clamp it back to 0 below. Once loaded, never
                // again: the service re-publishes on every resume too, and by
                // the second real snapshot mIndex already reflects wherever
                // the wearer has since cycled to.
                if (!mIndexLoaded && mState.count > 0) {
                    mIndex       = lastIndex();
                    mIndexLoaded = true;
                }
                // Read once, for the same reason and on the same trigger:
                // afterwards this list is ahead of the file, because
                // acknowledge() writes through to it.
                if (!mAcksLoaded && mState.count > 0) {
                    loadAcknowledgements();
                    mAcksLoaded = true;
                }
                // A re-read can leave fewer codes than before, so never trust
                // the old position: an index past the end would draw
                // somebody else's code.
                if (mIndex >= mState.count) {
                    mIndex = 0;
                }

                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                if (btn->event == Event::CLICK) {
                    switch (btn->id) {
                        case Id::SW4: // R2: back
                            LOG_INFO("Back pressed; exiting\n");
                            msg->setResult(SDK::MessageResult::SUCCESS);
                            mKernel.comm.releaseMessage(msg);
                            mKernel.sys.exit(0);
                            return;
                        case Id::SW1: // L1: previous
                            cycle(-1);
                            renderAndPush();
                            break;
                        case Id::SW3: // L2: next
                            cycle(+1);
                            renderAndPush();
                            break;
                        case Id::SW2: // R1: show a dense code anyway
                            if (warningShowing()) {
                                acknowledge();
                                renderAndPush();
                            }
                            break;
                        default:
                            break;
                    }
                }
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                mKernel.comm.sendResponse(msg);
                break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
    }
}
