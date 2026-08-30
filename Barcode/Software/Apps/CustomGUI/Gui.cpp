#include "Gui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

/// One flat, unwrapped string per Problem; Rust word-wraps it at the chosen
/// font (see lib.rs's word_wrap()). BadFormat is still built from
/// Symbology::kFormatNames so it keeps tracking the supported-format list
/// automatically -- the self-syncing property a past bug fix put there stays
/// intact. The other eight are static content with no data source to drift
/// from, so they are literals rather than anything generated.
const char *promptMessage(Barcode::Problem problem, char (&badFormatBuf)[96])
{
    using Barcode::Problem;
    switch (problem) {
    case Problem::NoValue:
        return "input.json has no usable code";
    case Problem::NotSet:
        return "No codes set yet. Open the UNA app and enter your ID";
    case Problem::BadValue:
        return "That ID cannot be drawn: 1-16 plain characters";
    case Problem::BadDigitCount:
        return "ITF needs an even count of digits, 2 to 16";
    case Problem::BadCharacters:
        return "ITF only draws digits 0-9";
    case Problem::BadWhitespace:
        return "That ID starts or ends with a space, remove it";
    case Problem::BadFormat: {
        int pos = std::snprintf(badFormatBuf, sizeof(badFormatBuf), "Unknown format. Set it to ");
        for (uint8_t i = 0; i < Barcode::kFormatCount && pos > 0 &&
                             static_cast<size_t>(pos) < sizeof(badFormatBuf); i++) {
            const char *sep = (i == 0) ? "" : ((i + 1 == Barcode::kFormatCount) ? " or " : ", ");
            pos += std::snprintf(badFormatBuf + pos, sizeof(badFormatBuf) - static_cast<size_t>(pos),
                                  "%s%s", sep, Barcode::kFormatNames[i]);
        }
        if (pos > 0 && static_cast<size_t>(pos) < sizeof(badFormatBuf)) {
            std::snprintf(badFormatBuf + pos, sizeof(badFormatBuf) - static_cast<size_t>(pos), ".");
        }
        return badFormatBuf;
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
        char badFormatBuf[sizeof(out.message)];
        const char *message = promptMessage(mState.problem, badFormatBuf);
        std::strncpy(out.message, message, sizeof(out.message) - 1);
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
                        case Id::SW2: // R1: unused, matches the original
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
