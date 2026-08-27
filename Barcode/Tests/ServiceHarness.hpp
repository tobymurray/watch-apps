/**
 ******************************************************************************
 * @file    ServiceHarness.hpp
 * @brief   The real Service, driven by a scripted message queue, with every
 *          state it publishes captured.
 ******************************************************************************
 *
 * The same reason SunGlance has GlanceHarness.hpp. SleepLab's glance sent
 * nothing for weeks while every piece of it built and passed its own tests,
 * because the bug lived in the joins rather than in any part. Barcode's joins
 * are short but they carry the whole app: a file becomes a message, and if the
 * message never leaves, the screen says there is no id and the file sitting
 * next to the binary says otherwise.
 *
 * `SDK::TestSupport::StubAppComm` is virtual, with a `getMessage()` that
 * returns nothing and a `sendMessage()` that drops what it is handed.
 * Overriding both lets the test be the kernel: queue what the GUI would send,
 * run `Service::run()` to completion, and read back every `BarcodeState` that
 * went out, in order. `Service` is unmodified and does not know this file
 * exists.
 *
 * `run()` loops until it is told to stop, so every script must end in a
 * COMMAND_APP_STOP or COMMAND_APP_NOTIF_GUI_STOP -- otherwise the queue drains,
 * getMessage() returns false, and the loop spins forever. queueStop() and
 * queueGuiStop() are there so a test cannot forget.
 *
 ******************************************************************************
 */

#ifndef BARCODE_TEST_SERVICEHARNESS_HPP
#define BARCODE_TEST_SERVICEHARNESS_HPP

#include <cstring>
#include <deque>
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "SDK/Messages/CommandMessages.hpp"

#include "Barcode.hpp"
#include "Commands.hpp"
#include "Service.hpp"

namespace BarcodeTest {

/// A message of an arbitrary type, with nothing in it.
///
/// `SDK::MessageBase`'s constructor is protected -- concrete message types are
/// meant to derive from it, which is what `CustomMessage::BarcodeRequest` does.
/// The harness needs to synthesise the kernel's own command types too, and
/// those have no app-side struct, so this is the minimum that makes one.
/// On the watch these come from a fixed pool, so `MessageBase` deletes
/// `operator new` and keeps its destructor protected -- an app cannot make one
/// on the heap. The harness is not an app, and the kernel side of the pool is
/// what it is standing in for, so the two are restored here for this type only.
/// `StubAppComm::releaseMessage()` frees with plain `::operator delete`, which
/// is what this pairs with.
struct ScriptedMessage : public SDK::MessageBase
{
    explicit ScriptedMessage(SDK::MessageType::Type type)
        : SDK::MessageBase(type)
    {
    }

    static void *operator new(size_t size) { return ::operator new(size); }
    static void operator delete(void *p) { ::operator delete(p); }
};

/// A kernel that delivers a scripted queue and remembers what came back.
class ScriptedComm : public SDK::TestSupport::StubAppComm
{
public:
    /// States published to the GUI, in the order they were sent.
    std::vector<Barcode::State> published;

    /// How many messages the service asked for, including the one that stopped
    /// it. Lets a test assert the loop did not spin.
    size_t getMessageCalls = 0;

    ~ScriptedComm() override
    {
        for (const Entry &e : mInbox) {
            delete e.msg;
        }
    }

    /// Queue a message with no payload beyond its type.
    void queueBare(SDK::MessageType::Type type)
    {
        Entry e{};
        e.msg = new ScriptedMessage(type);
        mInbox.push_back(e);
    }

    /**
     * @brief Queue something to happen between two messages.
     *
     * The point of the harness is the joins, and the most interesting join here
     * is "the file changed while the app was running". Without this, a test
     * could only set the file up before the run and would be asserting on a
     * situation that never arises -- the GUI resuming *after* somebody
     * rewrote input.json is the whole reason BARCODE_REQUEST exists.
     */
    void queueAction(std::function<void()> action)
    {
        Entry e{};
        e.action = std::move(action);
        mInbox.push_back(e);
    }

    void queueGuiRun() { queueBare(SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN); }
    void queueRequest() { queueBare(CustomMessage::BARCODE_REQUEST); }
    void queueStop() { queueBare(SDK::MessageType::COMMAND_APP_STOP); }
    void queueGuiStop() { queueBare(SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP); }

    /// Something the service has no case for, to check it is ignored rather
    /// than answered.
    void queueUnknown() { queueBare(static_cast<SDK::MessageType::Type>(0x7FFF0001)); }

    bool getMessage(SDK::MessageBase *&msg, uint32_t timeoutMs = 0xFFFFFFFF) override
    {
        (void)timeoutMs;
        getMessageCalls++;

        // Run any scripted side effects standing between here and the next
        // message.
        while (!mInbox.empty() && mInbox.front().msg == nullptr) {
            auto action = mInbox.front().action;
            mInbox.pop_front();
            if (action) {
                action();
            }
        }

        if (mInbox.empty()) {
            // The script ran out without stopping the service. Returning false
            // is what the real kernel does on timeout and would spin here, so
            // fail loudly instead by stopping the loop.
            ranDry = true;
            msg = new ScriptedMessage(SDK::MessageType::COMMAND_APP_STOP);
            return true;
        }
        msg = mInbox.front().msg;
        mInbox.pop_front();
        return true;
    }

    bool sendMessage(SDK::MessageBase *msg, uint32_t timeoutMs = 0) override
    {
        (void)timeoutMs;
        if (msg != nullptr && msg->getType() == CustomMessage::BARCODE_STATE) {
            published.push_back(static_cast<CustomMessage::BarcodeState *>(msg)->state);
        }
        return true;
    }

    /// True if the script was exhausted before the service was told to stop --
    /// a broken test rather than a broken app.
    bool ranDry = false;

private:
    /// Either a message to deliver or a side effect to run, never both.
    ///
    /// Held as the derived type, not as `MessageBase*`: the base destructor is
    /// protected, so an undelivered message can only be freed through
    /// `ScriptedMessage`.
    struct Entry
    {
        ScriptedMessage      *msg = nullptr;
        std::function<void()> action;
    };

    std::deque<Entry> mInbox;
};

/// A kernel fixture whose comm is the scripted one.
struct Harness
{
    SDK::TestSupport::StubSystem         system;
    SDK::TestSupport::StubLogger         logger;
    SDK::TestSupport::StubAppMemory      memory;
    ScriptedComm                         comm;
    SDK::TestSupport::InMemoryFileSystem fileSystem;
    SDK::Kernel                          kernel;

    Harness()
        : kernel(system, logger, memory, comm, fileSystem)
    {
    }

    /// Seeded at "/input.json", with the leading slash: BarcodeConfig::kConfigFile
    /// is the bare name "input.json", and SDK::AppConfig turns that into
    /// "/<name>" before it opens anything (AppConfig.cpp -- a configFile is a
    /// bare filename in the sandbox root by contract). Seeding the bare name
    /// leaves the service reporting NoConfig for a file that is right there.
    static constexpr const char *kConfigPath = "/input.json";

    void seed(const std::string &content) { fileSystem.seedFile(kConfigPath, content); }

    void removeFile() { fileSystem.files.erase(kConfigPath); }

    /// Run the real service to completion against the queued script.
    void run()
    {
        Service service(kernel);
        service.run();
    }

    const std::vector<Barcode::State> &published() const { return comm.published; }

    /// The first id in the nth published state, as a string. State grew from a
    /// single id to codes[kMaxCodes] + count in 84b9f03, so this is slot 0 and
    /// is empty when the state carries a problem instead of codes.
    std::string publishedId(size_t n) const
    {
        const Barcode::State &s = comm.published.at(n);
        return s.count == 0 ? std::string() : std::string(s.codes[0].id);
    }

    /// Every id in the nth published state, in the order the GUI will cycle
    /// them -- the property the multi-code service has to get right.
    std::vector<std::string> publishedIds(size_t n) const
    {
        const Barcode::State &s = comm.published.at(n);
        std::vector<std::string> out;
        for (uint8_t i = 0; i < s.count; i++) {
            out.emplace_back(s.codes[i].id);
        }
        return out;
    }

    /// How many codes the nth published state carries.
    uint8_t publishedCount(size_t n) const { return comm.published.at(n).count; }

    Barcode::Problem publishedProblem(size_t n) const { return comm.published.at(n).problem; }
};

/// The document the README tells a user to write. The field is "id1" and not
/// "id": the single-value schema became six numbered slots in 84b9f03, and
/// app-manifest.json declares id1..id6 / name1..name6.
inline std::string document(const std::string &id)
{
    return "{\n"
           "  \"schema\": 1,\n"
           "  \"values\": {\n"
           "    \"id1\": \"" + id + "\"\n"
           "  }\n"
           "}\n";
}

/// The same, with a value per slot -- for the property that only exists now
/// that there are six: what the service publishes, in what order.
inline std::string documentWithCodes(const std::vector<std::string> &ids,
                                    const std::vector<std::string> &names = {})
{
    std::string out = "{\n  \"schema\": 1,\n  \"values\": {\n";
    for (size_t i = 0; i < ids.size(); i++) {
        out += "    \"id" + std::to_string(i + 1) + "\": \"" + ids[i] + "\"";
        if (i < names.size()) {
            out += ",\n    \"name" + std::to_string(i + 1) + "\": \"" + names[i] + "\"";
        }
        out += (i + 1 < ids.size()) ? ",\n" : "\n";
    }
    return out + "  }\n}\n";
}

} // namespace BarcodeTest

#endif // BARCODE_TEST_SERVICEHARNESS_HPP
