/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Message contract between the Barcode service and its GUI.
 ******************************************************************************
 *
 * The service owns the id and answers every command with a full snapshot,
 * the same push-on-change convention Stopwatch uses: nothing is sent
 * periodically since there is nothing here that changes on its own.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Barcode.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage
{

// Service --> GUI
constexpr SDK::MessageType::Type BARCODE_STATE = 0x00000001;

// GUI --> Service
// 0x00000002 was BARCODE_SET_ID, a GUI-owned id. The id now comes from the
// provisioning file, and a GUI path that could overwrite it would only give
// the app a way to lose what the user provided.
constexpr SDK::MessageType::Type BARCODE_REQUEST = 0x00000003;

/**
 * @brief Full barcode state, service to GUI.
 */
struct BarcodeState : public SDK::MessageBase {
    Barcode::State state;
    BarcodeState()
        : SDK::MessageBase(BARCODE_STATE)
        , state(Barcode::makeUnsetState(Barcode::Problem::NoConfig))
    {}
};

/// The largest kernel message pool block. A whole state travels in one
/// message, so this is what actually bounds Barcode::kMaxCodes *and*
/// Barcode::kMaxIdLength, and raising either too far fails the build here
/// rather than truncating a wearer's codes.
constexpr size_t kPoolBlockBytes = 256;

/// What SDK::MessageBase costs on the watch, per the layout table and the
/// `sizeof(MessageBase) == 32` static_assert in SDK/Messages/MessageBase.hpp,
/// which that header applies only when `__SIZEOF_POINTER__ == 4`.
///
/// Stated here rather than measured because a 64-bit host build gets **40**
/// for the same struct -- its vptr and semaphore pointer double -- so a plain
/// `sizeof(BarcodeState)` is a different, stricter budget in the host tests
/// than on the target it is supposed to be about. Sizing the payload against
/// the watch's header is the question worth asking in both builds.
constexpr size_t kMessageHeaderOnWatch = 32;

static_assert(sizeof(Barcode::State) + kMessageHeaderOnWatch <= kPoolBlockBytes,
              "Barcode::State must fit the largest kernel message pool block -- "
              "lower Barcode::kMaxCodes, or shorten the id and name limits");

#if __SIZEOF_POINTER__ == 4
static_assert(sizeof(BarcodeState) <= kPoolBlockBytes,
              "BarcodeState must fit the largest kernel message pool block");
static_assert(sizeof(SDK::MessageBase) == kMessageHeaderOnWatch,
              "SDK::MessageBase is not the size the budget above assumes");
#endif

/**
 * @brief Ask the service to re-check the file and reply with the result.
 */
struct BarcodeRequest : public SDK::MessageBase {
    BarcodeRequest() : SDK::MessageBase(BARCODE_REQUEST) {}
};

/**
 * @class Sender
 * @brief Allocates, sends and releases the custom messages.
 */
class Sender
{
public:
    Sender(const SDK::Kernel &kernel)
        : mKernel(kernel)
    {}

    virtual ~Sender() = default;

    /**
     * @brief Service --> GUI: publish the current state.
     */
    bool state(const Barcode::State &state)
    {
        auto *msg = mKernel.comm.allocateMessage<BarcodeState>();
        if (!msg) {
            return false;
        }
        msg->state = state;
        const bool status = mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
        return status;
    }

    /**
     * @brief GUI --> Service: re-read the configuration and reply.
     *
     * Nothing tells an app that its values file was rewritten -- not from the
     * phone, not over USB -- so the GUI asking on every resume is the
     * notification. SDK::AppConfig reads once in its constructor and offers
     * no refresh, so the service answers by building a new one.
     */
    bool requestState() { return send<BarcodeRequest>(); }

private:
    template<typename T>
    bool send()
    {
        auto *msg = mKernel.comm.allocateMessage<T>();
        if (!msg) {
            return false;
        }
        const bool status = mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
        return status;
    }

    const SDK::Kernel &mKernel;
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
