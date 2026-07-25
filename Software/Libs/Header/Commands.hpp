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
constexpr SDK::MessageType::Type BARCODE_SET_ID  = 0x00000002;
constexpr SDK::MessageType::Type BARCODE_REQUEST = 0x00000003;

/**
 * @brief Full barcode state, service to GUI.
 */
struct BarcodeState : public SDK::MessageBase {
    Barcode::State state;
    BarcodeState()
        : SDK::MessageBase(BARCODE_STATE)
        , state(Barcode::makeDefaultState())
    {}
};

static_assert(sizeof(BarcodeState) <= 256,
              "BarcodeState must fit the largest kernel message pool block");

/**
 * @brief Ask the service to display a new id.
 */
struct BarcodeSetId : public SDK::MessageBase {
    Barcode::State state;
    BarcodeSetId() : SDK::MessageBase(BARCODE_SET_ID), state{} {}
};

static_assert(sizeof(BarcodeSetId) <= 256,
              "BarcodeSetId must fit the largest kernel message pool block");

/**
 * @brief Ask the service to reply with the current state.
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
     * @brief GUI --> Service: ask for a new id to be displayed.
     */
    bool setId(const Barcode::State &state)
    {
        auto *msg = mKernel.comm.allocateMessage<BarcodeSetId>();
        if (!msg) {
            return false;
        }
        msg->state = state;
        const bool status = mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
        return status;
    }

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
