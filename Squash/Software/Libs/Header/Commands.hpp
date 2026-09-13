#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Session.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage {

    // Service --> GUI
    constexpr SDK::MessageType::Type STATUS_UPDATE     = 0x00000001;
    constexpr SDK::MessageType::Type STATE_UPDATE      = 0x00000002;
    constexpr SDK::MessageType::Type ACCESSORY_STATUS  = 0x00000003;

    // GUI --> Service
    constexpr SDK::MessageType::Type SESSION_START     = 0x00000010;
    constexpr SDK::MessageType::Type SESSION_STOP      = 0x00000011;
    constexpr SDK::MessageType::Type SESSION_PAUSE     = 0x00000012;
    constexpr SDK::MessageType::Type SESSION_RESUME    = 0x00000013;
    constexpr SDK::MessageType::Type MARK              = 0x00000014;
    constexpr SDK::MessageType::Type SET_LABEL         = 0x00000015;

    /// One snapshot a second. One message rather than several because the GUI
    /// copies it straight into the struct its renderer takes, and a screen
    /// assembled from fields that arrived at different times can show a
    /// recorded-seconds count from one second beside a byte count from another.
    struct Status : public SDK::MessageBase {
        Session::Status status;

        Status() : SDK::MessageBase(STATUS_UPDATE), status{} {}
    };

    struct StateUpd : public SDK::MessageBase {
        Session::State state;
        /// Meaningful only on the transition out of a session: 1 the files are
        /// on disk, 0 they are not.
        uint8_t savedOk;

        StateUpd() : SDK::MessageBase(STATE_UPDATE), state{}, savedOk(0) {}
    };

    struct AccessoryStatusUpd : public SDK::MessageBase {
        uint8_t state;

        AccessoryStatusUpd() : SDK::MessageBase(ACCESSORY_STATUS), state(0) {}
    };

    struct SessionStart : public SDK::MessageBase {
        SessionStart() : SDK::MessageBase(SESSION_START) {}
    };

    struct SessionStop : public SDK::MessageBase {
        /// 1 = throw the session away. The recording is still closed out and
        /// kept either way: discarding a session does not make the samples
        /// less real.
        uint8_t discard;

        SessionStop() : SDK::MessageBase(SESSION_STOP), discard(0) {}
    };

    struct SessionPause : public SDK::MessageBase {
        SessionPause() : SDK::MessageBase(SESSION_PAUSE) {}
    };

    struct SessionResume : public SDK::MessageBase {
        SessionResume() : SDK::MessageBase(SESSION_RESUME) {}
    };

    /// "Note this instant", with no state asserted; written as kind NONE.
    struct Mark : public SDK::MessageBase {
        Mark() : SDK::MessageBase(MARK) {}
    };

    struct SetLabel : public SDK::MessageBase {
        /// A Session::Label. The Service clamps: a value the build does not
        /// know must not reach the file, because the file is what a later
        /// analysis trusts.
        uint8_t label;

        SetLabel() : SDK::MessageBase(SET_LABEL), label(0) {}
    };

// Helper wrapper
class Sender {
public:
    Sender(const SDK::Kernel &kernel) : mKernel(kernel) {}
    virtual ~Sender() = default;

    // Service --> GUI

    bool status(const Session::Status &s)
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::Status>()) {
            msg->status = s;
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool state(Session::State state, bool savedOk)
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::StateUpd>()) {
            msg->state   = state;
            msg->savedOk = savedOk ? 1u : 0u;
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool accessoryStatus(uint8_t state)
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::AccessoryStatusUpd>()) {
            msg->state = state;
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    // GUI --> Service

    bool sessionStart()
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::SessionStart>()) {
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool sessionStop(bool discard)
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::SessionStop>()) {
            msg->discard = discard ? 1u : 0u;
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool sessionPause()
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::SessionPause>()) {
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool sessionResume()
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::SessionResume>()) {
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool mark()
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::Mark>()) {
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

    bool setLabel(uint8_t label)
    {
        bool ok = false;
        if (auto *msg = mKernel.comm.allocateMessage<CustomMessage::SetLabel>()) {
            msg->label = label;
            ok = mKernel.comm.sendMessage(msg);
            mKernel.comm.releaseMessage(msg);
        }
        return ok;
    }

private:
    const SDK::Kernel &mKernel;
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
