/**
 ******************************************************************************
 * @file    SharedLog.hpp
 * @brief   Commits one session into ../SharedData/<app>_sessions.json.
 ******************************************************************************
 *
 * The half of TrainKit that touches a filesystem, and the only part of it that
 * is C++: the Rust in ../src decides what the file says, this decides how it
 * lands. Any activity Service can link both -- nothing here is Spin's.
 *
 * The store is a shared namespace, so this behaves like a guest: it owns one
 * filename per app, it never writes into another app's, and it refuses to
 * overwrite a file written to a schema it does not know.
 *
 ******************************************************************************
 */

#ifndef TRAINKIT_SHARED_LOG_HPP
#define TRAINKIT_SHARED_LOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "trainkit.h"

namespace TrainKit {

class SharedLog {
public:
    /// Longest app slug the filename is built from.
    static constexpr size_t kMaxAppLen = 24;

    /// @param app    names the file and appears in it; lowercased for the name.
    /// @param sport  what the sessions are, for a reader merging several apps'
    ///               logs. Not used in the filename.
    SharedLog(SDK::Interface::IFileSystem& fs, const char* app, const char* sport);

    enum class Status : uint8_t {
        OK = 0,
        /// The file was written by a schema this build does not know, and was
        /// left exactly as it was found.
        REFUSED,
        /// No room for the transient buffers, which is nothing this can fix.
        NO_MEMORY,
        /// The new file was written but could not be moved into place; the
        /// .tmp is deliberately left behind rather than deleted.
        COMMIT_FAILED,
        /// Nothing reached storage.
        WRITE_FAILED
    };

    /// Fold @p session into the store and commit it.
    ///
    /// Safe to interrupt: the new content is written to a .tmp and renamed over
    /// the real file, so a power loss leaves either the old file or the new one
    /// and never half of either.
    Status record(const trainkit_session& session);

private:
    bool readExisting(uint8_t* buf, uint32_t cap, uint32_t& outLen) const;
    bool writeTmp(const uint8_t* buf, uint32_t len);
    Status commit();

    /// @param suffix  "", ".tmp" or ".bak".
    void buildPath(char* out, size_t cap, const char* suffix) const;

    SDK::Interface::IFileSystem& mFs;
    /// Lowercased, because FAT is not reliably case-sensitive and two apps
    /// differing only in case would be one file.
    char mFileSlug[kMaxAppLen] = {};
    /// As the app spells itself, because this one is read by a person.
    char mApp[kMaxAppLen]      = {};
    char mSport[kMaxAppLen]    = {};
};

} // namespace TrainKit

#endif // TRAINKIT_SHARED_LOG_HPP
