/**
 ******************************************************************************
 * @file    GlanceHarness.hpp
 * @brief   The carousel, at a desk: the real Service, driven by a scripted
 *          message queue, with every glance update it sends captured.
 ******************************************************************************
 *
 * This is the test the SleepLab post-mortem asks for. That app's glance was
 * dead for weeks -- it sent nothing, ever -- and every individual piece of it
 * built, was reviewed, and passed its own tests. The failure lived entirely in
 * the joins: which call invalidates a control, which call marks the form clean,
 * and which of them the tick actually reaches. Nothing short of running the
 * real service and looking at what came out of it would have caught that.
 *
 * So: `SDK::TestSupport::StubAppComm` is a virtual class whose `getMessage()`
 * returns nothing and whose `sendMessage()` drops what it is given. Overriding
 * both lets the test *be* the kernel -- hand the service a glance start, a
 * sequence of ticks and a stop, answer its request for the glance area, and
 * keep every `RequestGlanceUpdate` it sends along with the text in each
 * control. `Service` is unmodified and does not know this file exists.
 *
 * The app's one concession is `Sun::setWallClockSource()`, and it buys the
 * whole point of the exercise: the interesting moments on this screen are the
 * minute before sunrise and the minute after sunset, which is not when anybody
 * runs a test suite.
 *
 ******************************************************************************
 */

#ifndef SUNGLANCE_TEST_GLANCEHARNESS_HPP
#define SUNGLANCE_TEST_GLANCEHARNESS_HPP

#include <cstdint>
#include <new>
#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "SDK/Glance/GlanceControl.h"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "Service.hpp"
#include "WallClock.hpp"

namespace SunGlanceTest {

/// One `RequestGlanceUpdate` as the kernel would have received it.
///
/// Split by control type rather than by index, because the two shapes of this
/// screen do not have the same controls: with icons the array is image, text,
/// image, text, text, and without them it is three texts. Reading a text out of
/// an image control would read the union's bytes as a string, which is exactly
/// the sort of thing that passes quietly.
struct Update
{
    std::string              name;
    std::vector<std::string> texts;
    std::vector<uint8_t>     textColors;
    std::vector<bool>        textVisible;
    std::vector<const uint8_t *> images;
    std::vector<bool>            imageVisible;

    /// The top row: the next event's time, or the whole message when the screen
    /// is one message instead.
    const std::string &first() const { return texts.at(0); }
    /// The row under it. Empty and hidden when there is no second event.
    const std::string &second() const { return texts.at(1); }
    bool secondShown() const { return textVisible.at(1); }
    const std::string &sub() const { return texts.at(2); }
    uint8_t subColor() const { return textColors.at(2); }

    const uint8_t *iconFirst() const { return images.at(0); }
    const uint8_t *iconSecond() const { return images.at(1); }
    bool iconsShown() const { return imageVisible.at(0); }
    bool secondIconShown() const { return imageVisible.at(1); }
};

/// A kernel that does exactly what the carousel does, and nothing else.
class ScriptedComm : public SDK::TestSupport::StubAppComm
{
public:
    /// What the glance area is. The defaults are what a watch reported on
    /// 2026-08-19 -- see glance.txt -- rather than what anybody assumed; a test
    /// can change them to check the app adapts or declines.
    int16_t  width       = 240;
    int16_t  height      = 60;
    uint32_t maxControls = 32;

    /// Everything the service sent, in order.
    std::vector<Update> updates;

    /// Script one glance viewing: start, @p ticks frames, stop.
    void viewing(int ticks)
    {
        mScript.push_back(SDK::MessageType::EVENT_GLANCE_START);
        for (int i = 0; i < ticks; i++) {
            mScript.push_back(SDK::MessageType::EVENT_GLANCE_TICK);
        }
        mScript.push_back(SDK::MessageType::EVENT_GLANCE_STOP);
    }

    /// Advance the scripted clock between frames. The callback runs just before
    /// the message at index @p before is handed over.
    void setClockStep(size_t before, int64_t seconds)
    {
        mSteps.push_back({ before, seconds });
    }

    bool getMessage(SDK::MessageBase *&msg, uint32_t timeoutMs = 0xFFFFFFFF) override
    {
        (void)timeoutMs;

        if (mNext >= mScript.size()) {
            // A service that has not returned by now is a service that missed
            // its stop; hand it another one rather than looping forever.
            msg = allocate(SDK::MessageType::EVENT_GLANCE_STOP);
            return msg != nullptr;
        }

        for (const Step &step : mSteps) {
            if (step.before == mNext) {
                sClock += step.seconds;
            }
        }

        msg = allocate(mScript[mNext++]);
        return msg != nullptr;
    }

    bool sendMessage(SDK::MessageBase *msg, uint32_t timeoutMs = 0) override
    {
        (void)timeoutMs;

        if (msg == nullptr) {
            return false;
        }

        switch (msg->getType()) {
            case SDK::MessageType::REQUEST_GLANCE_CONFIG: {
                auto *m = static_cast<SDK::Message::RequestGlanceConfig *>(msg);
                m->width       = width;
                m->height      = height;
                m->maxControls = maxControls;
                m->setResult(SDK::MessageResult::SUCCESS);
                return true;
            }
            case SDK::MessageType::REQUEST_GLANCE_UPDATE: {
                auto *m = static_cast<SDK::Message::RequestGlanceUpdate *>(msg);
                Update update;
                update.name = (m->name != nullptr) ? m->name : "";
                for (uint32_t i = 0; i < m->controlsNumber; i++) {
                    const GlanceControl_t &control = m->controls[i];
                    if (control.type == GLANCE_TYPE_TEXT) {
                        update.texts.push_back(control.payload.text.str);
                        update.textColors.push_back(control.payload.text.color);
                        update.textVisible.push_back(control.visible);
                    } else if (control.type == GLANCE_TYPE_IMAGE) {
                        update.images.push_back(control.payload.image.buff);
                        update.imageVisible.push_back(control.visible);
                    }
                }
                updates.push_back(update);
                m->setResult(SDK::MessageResult::SUCCESS);
                return true;
            }
            default:
                return true;
        }
    }

    // -- The scripted clock ---------------------------------------------------
    //
    // Static because `Sun::setWallClockSource` takes a plain function pointer,
    // which is the shape it is precisely so that the app carries no state for
    // the benefit of a test. One harness at a time, which is all a test needs.

    static int64_t sClock;
    static int64_t clock() { return sClock; }

private:
    struct Step
    {
        size_t  before;
        int64_t seconds;
    };

    /// Placement-new over the stub's pool, which is what the real
    /// `allocateMessage<T>()` does -- spelled out here because `StubAppComm`
    /// overrides the raw allocator and so hides the template that would
    /// otherwise call it.
    template<typename T>
    SDK::MessageBase *make()
    {
        void *memory = allocateMessage(sizeof(T));
        return (memory != nullptr) ? new (memory) T() : nullptr;
    }

    SDK::MessageBase *allocate(SDK::MessageType::Type type)
    {
        switch (type) {
            case SDK::MessageType::EVENT_GLANCE_START:
                return make<SDK::Message::EventGlanceStart>();
            case SDK::MessageType::EVENT_GLANCE_TICK:
                return make<SDK::Message::EventGlanceTick>();
            case SDK::MessageType::EVENT_GLANCE_STOP:
            default:
                return make<SDK::Message::EventGlanceStop>();
        }
    }

    std::vector<SDK::MessageType::Type> mScript;
    std::vector<Step>                   mSteps;
    size_t                              mNext = 0;
};

inline int64_t ScriptedComm::sClock = 0;

/// Stubs, a kernel over them, and one run of the real service.
struct Rig
{
    SDK::TestSupport::StubSystem         system;
    SDK::TestSupport::StubLogger         logger;
    SDK::TestSupport::StubAppMemory      memory;
    ScriptedComm                         comm;
    SDK::TestSupport::InMemoryFileSystem fs;
    SDK::Kernel                          kernel;

    Rig()
        : kernel(system, logger, memory, comm, fs)
    {
    }

    /// Write the config file the watch would have on it.
    void seed(const std::string &path, const std::string &content)
    {
        fs.seedFile(path, content);
    }

    /// Set the wall clock the run starts at, in UTC seconds.
    void at(int64_t utc) { ScriptedComm::sClock = utc; }

    /// Run the real `Service::run()` to completion against the script.
    void run()
    {
        Sun::setWallClockSource(&ScriptedComm::clock);
        Service service(kernel);
        service.run();
        Sun::setWallClockSource(nullptr);
    }

    const std::vector<Update> &updates() const { return comm.updates; }
};

} // namespace SunGlanceTest

#endif // SUNGLANCE_TEST_GLANCEHARNESS_HPP
