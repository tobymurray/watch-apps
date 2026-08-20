/**
 ******************************************************************************
 * @file    RestfulnessBand.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A relative index from movement and heart rate. NOT a sleep stage.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * ---------------------------------------------------------------------------
 * READ THIS BEFORE USING THE OUTPUT ANYWHERE
 *
 * This is **not** light sleep, deep sleep or REM. It is not a hypnogram, it
 * does not correspond to a sleep stage, and it must never be given
 * minutes-in-stage figures.
 *
 * Sleep staging rests on heart-rate variability plus multi-channel PPG. This
 * device has neither: `HEART_BEAT` (0x40) emits no events at all, so there are
 * no RR intervals to build HRV from, and the PPG waveform is single-channel.
 * Anything four-level and drawn across a night *looks* like a hypnogram, and
 * the temptation to relabel it as one is exactly why this comment is this
 * long.
 *
 * What it actually is: a four-level ordinal index over two channels this app
 * does record -- how much the wearer moved, and how far their heart rate sat
 * below their own quietest reading of the night. Both genuinely relate to how
 * settled a sleeper is. Neither identifies a stage.
 *
 * ---------------------------------------------------------------------------
 * Every file that carries a band also carries its method
 *
 * `kMethod` below is a version string written verbatim into every summary
 * JSON. Not decoration: a band drawn last month by a different rule is a
 * different measurement, and a file that does not say which rule produced it
 * cannot be compared with one that does. Change the rule, change the string --
 * and the JSON schema treats an unknown method string as a band it will not
 * plot rather than one it will plot wrongly.
 *
 * The band is also computed *relative to the night it is in*, not against any
 * absolute number: the reference heart rate is the wearer's own minimum from
 * that night. Two nights' bands are comparable in shape and not in level,
 * which is a real limitation and is stated where the band is displayed.
 *
 ******************************************************************************
 */

#ifndef ENGINE_RESTFULNESSBAND_HPP
#define ENGINE_RESTFULNESSBAND_HPP

#include <cstddef>
#include <cstdint>

#include "Engine/SleepWakeScorer.hpp"

namespace Engine
{

/**
 * @brief Four levels, because the framebuffer has four usable tones.
 *
 * The display is 8 bpp ABGR2222 -- two bits per channel, four luminance levels
 * -- so a strip drawn in more than four tones would band. That the physics of
 * the panel and the honesty of the data agree on four is a coincidence, but a
 * convenient one: four ordinal levels is about as much as this evidence can
 * carry anyway.
 *
 * Named for what they describe -- how settled the wearer was -- and
 * deliberately not for anything that could be mistaken for a stage.
 */
enum class Restfulness : uint8_t {
    Unknown = 0, ///< Not scorable: not worn, too few samples, or awake.
    Restless = 1,
    Settled  = 2,
    Deepest  = 3, ///< The *most settled* part of this night. Not "deep sleep".
};

/**
 * @brief Derive the band for a night.
 */
class RestfulnessBand
{
public:
    /**
     * @brief Written verbatim into every summary JSON.
     *
     * Bump the suffix whenever the rule below changes in any way that would
     * move a band. A reader that does not recognise the string must decline to
     * plot the band rather than plot it as though it were the current one.
     */
    static constexpr char kMethod[] =
        "movement+hr-relative-to-night-min, 4-level ordinal, not a sleep stage; v1";

    /// One-line description for the screen, so the caveat travels with the
    /// picture rather than living only in a file nobody opens.
    ///
    /// **It has to fit on one line of a round panel, and the old one did not.**
    /// "movement & heart rate - not sleep stages" renders 340 px wide at Poppins
    /// Medium 16. The caption sits below the strip, where the glass is already
    /// narrowing, and the widest box that fits there is 164 px -- so what
    /// actually reached the wearer was about "movement & heart", which names
    /// the inputs and drops the disclaimer. A caption that is cut in half says
    /// the opposite of what it is for.
    ///
    /// So the half that survives is the half that matters. What the band is
    /// made of is in `kMethod`, in every summary JSON, in the README and in the
    /// ledger; what it is *not* has one place to be said and about twenty
    /// characters to say it in.
    ///
    /// `kMethod` is deliberately unchanged: it is the file-level provenance
    /// string, it is not width-constrained, and moving it would make every night
    /// already on disk incomparable with every night after this.
    static constexpr char kCaption[] = "not sleep stages";

    /// Counts per scoring epoch at or below which movement contributes its
    /// most-settled score.
    /// TODO: this and kMoveRestless want the same diary-validated recording
    /// that calibrates NightAnalyser::kMovementFloor; they are currently
    /// guesses placed either side of it.
    static constexpr uint32_t kMoveSettled  = 20;
    /// Counts per scoring epoch at or above which movement contributes its
    /// least-settled score.
    static constexpr uint32_t kMoveRestless = 120;

    /// Heart rate at or below (night minimum + this, in bpm x10) contributes
    /// the most-settled score.
    ///
    /// Relative to the wearer's own minimum *for this night*, never to an
    /// absolute bpm. 20 (= 2.0 bpm) and 80 (= 8.0 bpm) bracket a range that is
    /// small against typical nocturnal HR variation.
    /// TODO: justify from recorded nights. Guesses.
    static constexpr int32_t kHrSettledX10  = 20;
    /// Heart rate at or above (night minimum + this) contributes the
    /// least-settled score.
    static constexpr int32_t kHrRestlessX10 = 80;

    /**
     * @brief Compute one band value per epoch.
     *
     * @param in    Scoring epochs.
     * @param v     Sleep/wake verdicts. Epochs scored Wake or Unscorable get
     *              `Unknown` -- a restfulness value for an epoch the wearer was
     *              awake in describes nothing.
     * @param n     How many.
     * @param out   Receives one Restfulness per epoch. Must hold @p n.
     * @param hrMinX10 The night's own heart-rate minimum, or Engine::kAbsent.
     *              With no heart rate the band is derived from movement alone,
     *              which is a materially weaker measurement -- see
     *              `usedHeartRate()`.
     * @return      Whether heart rate contributed at all. The caller writes
     *              this into the summary JSON alongside the method string,
     *              because "movement and heart rate" and "movement" are not
     *              the same method and the file must not claim the first when
     *              it did the second.
     */
    static bool compute(const ScoringInput *in, const Verdict *v, size_t n,
                        int32_t hrMinX10, Restfulness *out);
};

} // namespace Engine

#endif // ENGINE_RESTFULNESSBAND_HPP
