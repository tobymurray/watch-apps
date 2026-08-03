/**
 ******************************************************************************
 * @file    ImuFileSink.hpp
 * @brief   ImuCsvRecorder sink over the SDK file system.
 ******************************************************************************
 */

#ifndef IMU_FILE_SINK_HPP
#define IMU_FILE_SINK_HPP

#include <cstddef>
#include <ctime>
#include <memory>

#include "SDK/Kernel/Kernel.hpp"

#include "ImuCsvRecorder.hpp"

/**
 * @class ImuFileSink
 *
 * The device-side half of the research recorder: ImuCsvRecorder formats rows
 * and enforces the caps, this puts them on storage. The split exists so the
 * recorder stays SDK-free and host-testable against a memory buffer.
 *
 * Recordings land in their own directory, *not* under Activity/. They are
 * research inputs, not activities: nothing should present them to the user as
 * a workout, and they must not ride along with whatever syncs the activity
 * tree. The layout mirrors ActivityWriter's (YYYYMM/ subdirectory, timestamped
 * file) so a session's CSV is easy to pair with its .fit by name.
 */
class ImuFileSink final : public ImuCsvRecorder::ISink {
public:
    /**
     * @param kernel    Kernel, for its file system.
     * @param pathToDir Directory to create recordings under, e.g. "Imu".
     */
    ImuFileSink(const SDK::Kernel& kernel, const char* pathToDir);

    ~ImuFileSink() override;

    ImuFileSink(const ImuFileSink&)            = delete;
    ImuFileSink& operator=(const ImuFileSink&) = delete;

    /**
     * @brief Create and open this session's CSV.
     * @param utc Wall-clock start time, used for the file name only.
     * @return true if the file is open and writable.
     *
     * Any previously open file is closed first, so a caller that skipped
     * close() cannot leak a handle across sessions.
     */
    bool create(std::time_t utc);

    /// Flush and close. Safe when nothing is open.
    bool close();

    bool isOpen() const { return mFile != nullptr; }

    /// Path of the open (or last opened) file; nullptr if never opened.
    const char* path() const;

    // -- ImuCsvRecorder::ISink -----------------------------------------------

    /// Short writes are reported as failure: a torn row would desync the CSV.
    bool write(const char* data, size_t len) override;

    bool flush() override;

private:
    const SDK::Kernel& mKernel;
    const char*        mDir = nullptr;

    std::unique_ptr<SDK::Interface::IFile> mFile;
};

#endif // IMU_FILE_SINK_HPP
