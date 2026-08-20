/**
 ******************************************************************************
 * @file    NightStore.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A night on disk: the epoch log, the resume state, and the index.
 ******************************************************************************
 *
 * NORMATIVE FORMAT for everything this app writes. `SleepLab/README.md`
 * summarises it; this comment is the spec.
 *
 * ---------------------------------------------------------------------------
 * The files
 *
 *   Nights/<start>.csv     One row per 30 s recording epoch. The record.
 *   Nights/<start>.json    The summary, written once when the night closes.
 *   Nights/index.csv       One row per completed night. The history.
 *   night_state.txt        Present only while a night is in progress.
 *
 * `<start>` is `YYYYMMDDTHHMMSS` **local**, from the wall clock when the
 * session opened. So a night is named for the evening it began, not the
 * morning it ended, and the README says so once rather than leaving it to be
 * inferred. Naming by the morning would be friendlier to read and is not
 * computable at the moment the file has to be created.
 *
 * A night with no readable wall clock is named `unknownNNN`, numbered from the
 * resume state. It still records; it simply cannot be filed by date.
 *
 * ---------------------------------------------------------------------------
 * Why there is an index, rather than listing the directory
 *
 * `SDK::Interface::IFileSystem` does offer directory enumeration, and the
 * history screen could use it. It deliberately does not, for two reasons:
 *
 *   - The SDK's stock in-memory filesystem fake ships `EmptyDirectory`, which
 *     always reports no entries. The real enumerating fake exists only on
 *     `una-sdk`'s `poc/athensrun` branch, so anything on the enumeration path
 *     cannot be host-tested without pinning this repo's test suite to an
 *     unmerged SDK branch. `MapManager/Tests` already has that problem.
 *   - Enumerating a decade of nights to draw five rows means reading 3650
 *     directory entries and then opening some of them. An index row is ~110
 *     bytes; twenty-eight nights of history is one 3 KB read.
 *
 * `FwDump` made the same call for its resume scan and noted it was also more
 * robust on hardware.
 *
 * The index is the **only** persisted state the baseline is built from, so
 * there is one source of truth rather than an index and a baseline file that
 * can disagree.
 *
 * ---------------------------------------------------------------------------
 * Surviving a restart
 *
 * Plugging in USB terminates every running app and autostart relaunches it on
 * unplug. A night therefore has to survive a process that vanishes without
 * warning, which is the same problem `FwDump` solved and the same discipline:
 *
 *   - Every epoch row is appended, flushed and the handle closed. No handle is
 *     held across the night. 960 opens a night is nothing next to losing one.
 *   - `night_state.txt` is rewritten after every flush and names the file in
 *     progress, the epoch count, and **both clocks**.
 *   - On start, a state file means a night was in progress: the recorder
 *     re-opens that file and continues it rather than starting a second.
 *
 * The (uptime, wall-clock) pair in the state file is what makes the stitch
 * honest. Uptime that has gone *backwards* since the state was written means
 * the device rebooted; uptime that has climbed means the app restarted inside
 * one boot. A wall clock that moved by more than uptime says it should have
 * means the clock was changed. All three mark the night interrupted, and the
 * summary says which.
 *
 ******************************************************************************
 */

#ifndef NIGHTSTORE_HPP
#define NIGHTSTORE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Engine/BaselineStore.hpp"
#include "Engine/Epoch.hpp"
#include "Engine/NightSummary.hpp"

namespace SleepLab
{

/// Directory, relative to the app's sandbox. Created on first use.
constexpr char kNightsDir[]  = "Nights";
/// One row per completed night.
constexpr char kIndexPath[]  = "Nights/index.csv";
/// Present only while a night is in progress.
constexpr char kStatePath[]  = "night_state.txt";

/// What the segmenter was looking at before a night opened, or instead of one.
///
/// Same header and same row format as a night's own epoch log, so
/// `Tools/night_report.py thresholds` reads it with no changes -- which is the
/// whole point of it.
///
/// The pre-roll ring holds thirty minutes of epochs while idle and used to throw
/// them away, so two questions had no answer on the volume: **why did no night
/// open**, and **what does a still wrist actually count**. The second is the one
/// that matters, because `SegmenterConfig::stillnessCountMax` is a guess at about
/// 2 mg of band-limited movement -- the same order as the sensor's own in-band
/// noise -- and if the noise is above it no night ever opens, which from the
/// outside is indistinguishable from a wearer who did not go to bed.
///
/// Recording them means a night that fails to open is a *measurement* of the noise
/// floor rather than a wasted night, and the threshold can be set from a
/// distribution instead of moved on another guess.
constexpr char kWatchingPath[] = "Nights/watching.csv";

/// Truncated and restarted past this. A 14-hour window with no night is ~1 700
/// rows at ~117 bytes, so ~200 KB; 1 MB holds several windows even if the
/// truncate-on-window-entry below never fires because the clock is unreadable.
constexpr size_t kWatchingMaxBytes = 1024u * 1024u;

/// Longest a generated path can be, including the terminator.
constexpr size_t kMaxNightPath = 64;

/// Schema of the epoch CSV. Bumped when a column is added, removed or
/// reinterpreted; a reader must refuse an unknown schema rather than mapping
/// columns by position and reporting the wrong sensor's numbers.
///
/// **3, and it was 2 for one release too long.** `kEpochHeader` in
/// `NightStore.cpp` gained `count_x,count_y,count_z` and said "schema 3" in its
/// own comment line, and this constant -- the one machine-readable copy, written
/// into every summary as `epochs_csv_schema` -- was not bumped with it. So a
/// file carried 35 columns and declared 32, and a reader obeying the rule
/// directly above would either refuse a good file or accept it and silently
/// miss three columns. The whole point of declaring a schema is that the
/// declaration and the columns are checked against each other, and nothing was
/// checking these two.
///
/// The rule that follows, for the next column: **the header literal and this
/// constant move in the same commit.** `Tests/NightStore_test.cpp` now asserts
/// they agree, so forgetting is a test failure rather than a discovery six
/// months later.
constexpr uint32_t kEpochCsvSchema = 3;
/// Schema of the index CSV. Independent of the above: they change for
/// different reasons and a decade of index rows should survive an epoch column
/// being added.
constexpr uint32_t kIndexCsvSchema = 1;
/// Schema of the summary JSON.
constexpr uint32_t kSummaryJsonSchema = 1;

/// The app version written into every summary, so a file says which build
/// produced it.
///
/// Deliberately a literal here rather than the `APP_VERSION` the ARM build
/// defines: the TouchGFX simulator builds the same sources without it, and a
/// provenance field that only exists under one of two build systems is worse
/// than one that is maintained by hand. Bump it whenever anything that would move
/// a number changes -- a threshold, a filter, an epoch length.
constexpr char kAppVersion[] = "0.3.0";

/**
 * @brief What a resumed launch found on disk.
 */
struct ResumeState
{
    bool     present   = false;  ///< A night was in progress.
    char     path[kMaxNightPath] = {}; ///< Its epoch CSV.
    uint32_t epochs    = 0;      ///< Recording epochs already written.
    uint32_t uptimeMs  = 0;      ///< Uptime at the last flush.
    int64_t  wallUtc   = 0;      ///< Wall clock at the last flush, or -1.
    /// Wall clock when the session *opened*, or -1.
    ///
    /// Carried in the state file rather than re-derived, because a resumed
    /// night's start is otherwise simply lost -- and it is the night's
    /// identity: it names the file and it is what the history sorts on.
    int64_t  startUtc  = 0;
    uint16_t flags     = 0;      ///< Interruption bits accumulated so far.

    /// Set by `readState()` when the recovered state implies the app or the
    /// device restarted. Folded into the night's interruption flags.
    bool     deviceRebooted = false;
    bool     appRestarted   = false;

    /// Minutes of the night that passed while the app was not running.
    ///
    /// Real minutes of the session with no record at all: not in the CSV,
    /// because nothing was recording, and not in RAM either. They have to be
    /// counted, because every epoch index the summary reports is turned into a
    /// time of day by counting minutes from the session's start -- and a gap
    /// nobody counted moves everything after it earlier by the length of the
    /// gap. A USB session is normally about the length of a copy, and this is
    /// what stops that length being subtracted from the night.
    ///
    /// Taken from uptime for an app restart, and from the wall clock for a
    /// device reboot, which has no uptime to measure against. Zero when neither
    /// clock can say.
    uint32_t gapMinutes    = 0;
};

/**
 * @brief Writes and reads everything a night leaves on disk.
 *
 * One instance for the life of the service. Holds no file handles between
 * calls -- see the file comment on why.
 */
class NightStore
{
public:
    explicit NightStore(const SDK::Kernel &kernel);

    // -- Starting and resuming ------------------------------------------------

    /**
     * @brief Look for a night in progress.
     *
     * @param nowMs   Uptime now, for the restart classification.
     * @param nowUtc  Wall clock now, or -1.
     */
    ResumeState readState(uint32_t nowMs, int64_t nowUtc);

    /**
     * @brief Begin a new night, creating its epoch CSV with a header.
     *
     * @param startUtc  Wall clock at session open, or -1. Names the file.
     * @param nowMs     Uptime at session open.
     * @param provenance One line describing what is recording: the build, the
     *                  settings in force, the heart-rate mode. Written into the
     *                  CSV's own header as a comment.
     *
     *                  In the CSV rather than only in the summary JSON, because
     *                  the JSON is written when a night *closes* -- so an
     *                  interrupted night used to leave a record that could not say
     *                  what produced it. ~120 bytes, once.
     * @retval true     The file exists and carries its header.
     */
    bool beginNight(int64_t startUtc, uint32_t nowMs,
                    const char *provenance = nullptr);

    /// Continue the night named by @p state.
    bool resumeNight(const ResumeState &state);

    /// Whether a night is open for writing.
    bool isOpen() const { return mPath[0] != '\0'; }
    /// The open night's epoch CSV path, or "".
    const char *path() const { return mPath; }
    /// Recording epochs written to the open night, resumed ones included.
    uint32_t epochsWritten() const { return mEpochs; }

    // -- Recording ------------------------------------------------------------

    /**
     * @brief Append one epoch and rewrite the resume state.
     *
     * Both, every time. A state file that lags the data by even one flush
     * would resume a night at the wrong length, and the epoch count is what
     * the summary's time-in-bed is built from.
     *
     * @param flags Interruption bits known so far, persisted with the state so
     *              a restart does not forget that the charger was seen.
     */
    bool appendEpoch(const Engine::Epoch &e, uint16_t flags);

    /**
     * @brief Append one epoch to `watching.csv` -- the idle record.
     *
     * For epochs the segmenter saw while **no** night was open. Same row format as
     * a night's, so the same tooling reads both.
     *
     * @param restart Start the file again, header and all, before appending. The
     *                caller passes true on entering the bedtime window, so the
     *                file holds one window's worth rather than growing across
     *                every evening the app has ever run.
     *
     * Failure is not reported, deliberately. This file is diagnostic: an idle row
     * that could not be written must never be able to affect a night, and the case
     * where it cannot be written is itself visible, because the file is absent.
     */
    void appendWatching(const Engine::Epoch &e, bool restart = false);

    // -- Finishing ------------------------------------------------------------

    /**
     * @brief Write the summary JSON, append the index row, clear the state.
     *
     * In that order, and the order matters: the state file is what says "a
     * night is in progress", so clearing it last means a crash anywhere in
     * here resumes the night rather than losing it. The cost of the state
     * outliving the summary is one duplicate index row, which is visible;
     * the cost of the reverse is a silently lost night.
     *
     * @param bandMethod   RestfulnessBand::kMethod, written verbatim so every
     *                     file says which rule drew its band.
     * @param bandUsedHr   Whether heart rate actually contributed to the band.
     * @param hrMode       The HR mode the night ran under, for the record.
     */
    bool finishNight(const Engine::NightSummary &s, const char *bandMethod,
                     bool bandUsedHr, const char *hrMode);

    /**
     * @brief Abandon the open night without summarising it.
     *
     * For a session too short to report. The epoch CSV is deliberately left on
     * disk -- it is real data somebody may want -- but no index row is written,
     * so it never reaches the history or the baseline.
     */
    void discardNight();

    // -- History --------------------------------------------------------------

    /// One row of the index, as the history screen shows it.
    struct IndexRow
    {
        int64_t  startUtc      = 0;
        int32_t  timeInBedMin  = Engine::kAbsent;
        int32_t  totalSleepMin = Engine::kAbsent;
        int32_t  efficiencyPct = Engine::kAbsent;
        int32_t  hrMinX10      = Engine::kAbsent;
        int32_t  hrMinAtPct    = Engine::kAbsent;
        uint8_t  worn          = 0;  ///< Engine::WornVerdict.
        uint16_t interruption  = 0;
    };

    /// Nights kept in memory when reading the index. Matches the baseline
    /// window, since rebuilding the baseline is what this is mostly for.
    static constexpr size_t kMaxHistory = Engine::BaselineStore::kWindowNights;

    /**
     * @brief Read the most recent nights from the index.
     *
     * Reads the file's tail rather than the whole thing: at ~110 bytes a row a
     * decade is ~400 KB, and the history never needs more than the window.
     *
     * @param out    Receives up to kMaxHistory rows, oldest first.
     * @param maxOut Capacity of @p out.
     * @return       Rows written.
     */
    size_t readHistory(IndexRow *out, size_t maxOut) const;

    /**
     * @brief Rebuild the personal baseline from the index.
     *
     * The index is the single source of truth: there is no separate baseline
     * file to fall out of step with it.
     *
     * **Nights that failed the worn gate are excluded.** A nightstand's
     * flawless efficiency in the baseline would poison it for four weeks and
     * make every real night afterwards look bad.
     */
    void loadBaseline(Engine::BaselineStore &out) const;

private:
    bool append(const char *path, const char *text, size_t len);
    bool writeState(uint16_t flags);

    const SDK::Kernel &mKernel;

    char     mPath[kMaxNightPath] = {};
    uint32_t mEpochs = 0;

    /// Both clocks at the last epoch written, persisted into the state file.
    /// The pair is what lets a resumed night be stitched honestly: uptime says
    /// whether the app or the device restarted, and the wall clock says
    /// whether it moved under us while we were gone.
    uint32_t mLastUptimeMs = 0;
    int64_t  mLastWallUtc  = 0;

    /// Wall clock when this night opened. The index's `start_utc` column, and
    /// what the history sorts on -- not the last epoch's clock, which is what
    /// every night would share if the recorder happened to be fed the same
    /// epoch twice.
    int64_t  mStartWallUtc = 0;

    /// Distinguishes clockless nights within one launch. Only used to name
    /// them; a night with no readable wall clock still records, it just cannot
    /// be filed by date.
    uint32_t mUnnamedSeq = 0;
};

} // namespace SleepLab

#endif // NIGHTSTORE_HPP
