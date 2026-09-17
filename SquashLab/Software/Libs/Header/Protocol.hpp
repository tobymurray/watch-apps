/**
 ******************************************************************************
 * @file    Protocol.hpp
 * @brief   The drill protocols, read from a file the wearer edits.
 ******************************************************************************
 *
 * A protocol is a list of drills: what to hit, how many, and what number goes
 * in the recording's marker sidecar when it starts. It is data rather than
 * code because the questions change faster than the app does -- forehand
 * against backhand this week, straight against crosscourt next -- and a court
 * is a bad place to discover you needed a rebuild.
 *
 * Fixed storage, no allocator, and every limit refuses rather than truncates:
 * a protocol silently missing its last three drills would be discovered as a
 * session of the wrong shape, hours later, with no way to tell which drills
 * were recorded.
 *
 ******************************************************************************
 */

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

class Protocols {
public:
    /// Longest name, with the terminator. Matches the renderer's NAME_LEN, and
    /// a static_assert in the GUI holds the two together.
    static constexpr size_t skNameLen = 16;
    static constexpr uint8_t skMaxSteps = 24;
    static constexpr uint8_t skMaxProtocols = 8;
    /// The largest file this will read. A protocol file is a few kilobytes;
    /// anything past this is not one, and reading it would cost the recording
    /// its buffer.
    static constexpr size_t skMaxFileBytes = 8u * 1024u;

    /// The kind values 0-15 belong to RecorderKit's session states, so a drill
    /// step is either one of those -- a rest is REST, the same state a match
    /// recording writes -- or a shot at 16 and above. A step claiming anything
    /// else is refused, because the number reaches the file and a wrong one is
    /// silent and permanent.
    static constexpr uint8_t skFirstShotKind = 16;

    struct Step {
        char     line1[skNameLen] = {};
        char     line2[skNameLen] = {};
        /// Written to the marker sidecar when this drill starts.
        uint8_t  kind    = 0;
        /// Shots asked for; 0 = timed rather than counted.
        uint16_t target  = 0;
        /// Seconds the drill runs for; 0 = until the wearer advances.
        uint16_t seconds = 0;
    };

    struct Protocol {
        char    name[skNameLen] = {};
        Step    steps[skMaxSteps];
        uint8_t stepCount = 0;
    };

    /// Why a file produced nothing.
    enum class Error : uint8_t {
        NONE = 0,
        MISSING,        ///< No file at that path.
        TOO_LARGE,      ///< Past skMaxFileBytes.
        UNREADABLE,     ///< Present but the read failed.
        MALFORMED,      ///< Not JSON, or not this schema.
        NO_PROTOCOLS,   ///< Valid, and empty.
    };

    Protocols() = default;

    Protocols(const Protocols&)            = delete;
    Protocols& operator=(const Protocols&) = delete;

    /**
     * @brief Read and parse the protocol file.
     * @return false on any failure; lastError() says which, and the previous
     *         contents are left alone so a bad edit does not empty the picker.
     */
    bool load(const SDK::Kernel& kernel, const char* path);

    uint8_t count() const { return mCount; }
    Error   lastError() const { return mError; }

    /// Protocol at @p index, or nullptr when there is none.
    const Protocol* at(uint8_t index) const
    {
        return (index < mCount) ? &mProtocols[index] : nullptr;
    }

private:
    /// Copy a JSON string into a fixed name field, terminated and truncated.
    ///
    /// Truncation is right for a name and wrong for a count, which is why the
    /// numbers refuse and this does not: a shortened name is still legible on
    /// the screen, where a shortened protocol is a different experiment.
    static void copyName(char (&dest)[skNameLen], const char* src, size_t len);

    Protocol mProtocols[skMaxProtocols];
    uint8_t  mCount = 0;
    Error    mError = Error::NONE;
};

#endif // PROTOCOL_HPP
