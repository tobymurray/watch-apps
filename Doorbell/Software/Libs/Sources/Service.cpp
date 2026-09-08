#include "Service.hpp"

#include "Commands.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#define LOG_MODULE_PRX   "DoorbellSvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
// Long enough to block properly, short enough that the heartbeat is readable.
constexpr uint32_t kPollMs = 1000;

// After the GUI closes, one ring with nothing on screen. If the wire carries it,
// egress does not need a foreground app; if it does not, that is the answer to
// the same question.
constexpr uint32_t kOrphanRingAfterMs = 30000;

// A service that outlives its GUI forever is a stuck process, so it gets a
// deadline. Ten minutes is long enough to show whether the kernel reaps it first.
constexpr uint32_t kOrphanExitAfterMs = 600000;

constexpr uint32_t kHeartbeatMs = 30000;
} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Service::ring(uint32_t seq)
{
    const bool sent = SDK::send_msg<SDK::Message::CommandAppNewActivity>(mKernel);
    LOG_INFO("ring seq=%u t=%u sent=%d\n", static_cast<unsigned>(seq),
             static_cast<unsigned>(mKernel.sys.getTimeMs()), static_cast<int>(sent));
}

void Service::run()
{
    // A log line proves nothing unless it says which build wrote it.
    LOG_INFO("Started v%s id=%s\n", BUILD_VERSION, APP_ID);

    // Zero means the GUI is still up. Everything orphan-related hangs off this.
    uint32_t guiStoppedAt = 0;
    bool     orphanRung   = false;
    uint32_t lastBeat     = mKernel.sys.getTimeMs();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (mKernel.comm.getMessage(msg, kPollMs)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Stopped by kernel t=%u\n",
                             static_cast<unsigned>(mKernel.sys.getTimeMs()));
                    mKernel.comm.releaseMessage(msg);
                    return;

                // Deliberately not an exit. Whether this process is still
                // scheduled after its GUI goes away is the thing being measured.
                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    guiStoppedAt = mKernel.sys.getTimeMs();
                    LOG_INFO("GUI stopped t=%u, staying up\n",
                             static_cast<unsigned>(guiStoppedAt));
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    guiStoppedAt = 0;
                    orphanRung   = false;
                    LOG_INFO("GUI running t=%u\n",
                             static_cast<unsigned>(mKernel.sys.getTimeMs()));
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    break;

                case CustomMessage::RING_FROM_SERVICE:
                    ring(static_cast<CustomMessage::RingFromService *>(msg)->seq);
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    break;

                default:
                    msg->setResult(SDK::MessageResult::FAIL);
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        const uint32_t now = mKernel.sys.getTimeMs();

        if (now - lastBeat >= kHeartbeatMs) {
            lastBeat = now;
            LOG_INFO("alive t=%u guiStoppedAt=%u\n", static_cast<unsigned>(now),
                     static_cast<unsigned>(guiStoppedAt));
        }

        if (guiStoppedAt != 0) {
            if (!orphanRung && (now - guiStoppedAt >= kOrphanRingAfterMs)) {
                orphanRung = true;
                LOG_INFO("orphan ring, no GUI on screen\n");
                ring(0);
            }
            if (now - guiStoppedAt >= kOrphanExitAfterMs) {
                LOG_INFO("orphan deadline reached t=%u, exiting\n",
                         static_cast<unsigned>(now));
                return;
            }
        }
    }
}
