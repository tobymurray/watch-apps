/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    30-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   One glance, one rectangle, drawn at a fixed inset from the edges.
 ******************************************************************************
 *
 * This app exists to answer a question no amount of reading the SDK can: how
 * much of the glance area the kernel reports is actually *drawable*, and how
 * much of it something else -- the carousel's scroll indicator, a bezel, a
 * border the kernel paints over the top -- takes back.
 *
 * `SunGlance` had to answer it twice by shipping a build to a watch and looking
 * at what was cut off, which is a slow way to learn a number. Its answer is the
 * pair of constants `kSafeLeftInset = 18` and `kSafeRight = 8`, and the honest
 * comment on both is that eighteen is "what SleepLab's main lines use and render
 * cleanly, so it is a measurement rather than a guess" -- true, but it is a
 * measurement of *another app looking fine*, not of where the edge is.
 *
 * ## What one build draws
 *
 * An unfilled rectangle inset `PROBE_INSET` pixels from every edge of the
 * reported area, and two lines of text saying which build this is and what the
 * kernel said the area was. Three controls, which is `kControlsNeeded` in every
 * SDK glance example and so the budget any kernel should grant.
 *
 * ## Why it is a series and not one app with a setting
 *
 * A glance has no buttons -- the carousel owns them, and the service is stopped
 * the moment the card scrolls away -- so there is no way to step an inset from
 * the watch. A config file could do it, but then reading the answer means
 * editing a file, power-cycling, and holding the previous screen in your head to
 * compare against. Eight builds at eight insets turn that into scrolling: the
 * carousel puts them side by side in time, and the first card whose border is
 * whole on all four sides is the answer. Which *edge* is broken on the cards
 * that fail gives the per-side numbers for free.
 *
 * `PROBE_INSET` is a compile definition rather than a constant here, because
 * that is what makes the eight builds one source file.
 *
 * ## The ruler (PROBE_STYLE=ruler)
 *
 * The staircase worked -- it drew, and both edges came back as clean staircases
 * -- but reading it still meant measuring photo pixels, and that is where it
 * came apart. Two independent calibrations of the same photograph disagreed by
 * a factor of two: the bar pitch said 7.5 photo pixels per display pixel and the
 * bar height said 3.7. Nothing about a curved glass photographed hand-held
 * settles that.
 *
 * So the ruler stops measuring and starts *naming*. Six adjacent 1px columns,
 * one per display pixel from the edge inward, each a different colour:
 *
 *     column  0   1   2   3   4   5
 *     colour  R   G   B   Y   C   W
 *
 * The outermost stripe that survives names its own column. "The outermost
 * stripe is blue" means column 2 is the first that draws, and the drawable inset
 * is 2 -- no scale, no pixel arithmetic, no argument about what the camera was
 * doing. Six columns because round 1 bracketed the answer between 0 and 4, so
 * this covers it with margin.
 *
 * Both edges carry the same six, which is the other half of the point. The right
 * edge is clean glass. The left edge is glass *plus* the carousel's scroll arc,
 * which is painted over the glance rather than beside it. Comparing the two
 * separates what can be drawn from what can be read -- two different questions
 * that `kSafeLeftInset` currently answers with one number.
 *
 * ## The arc probe (PROBE_STYLE=arc)
 *
 * The ruler's colours did not survive the panel. It is a backlit LCD at two bits
 * a channel and the backlight is blue, so every hue is dragged toward it: red
 * came back as a magenta tinge at the extreme edge, green landed on cyan and
 * could not be told from white, and the bright colours bloomed over the dark
 * single-channel ones. Six hues chosen to be distinct in RGB222, all of them
 * collapsing into one channel on the glass. Hue is not a usable encoder here.
 *
 * Luminance is. This style paints the whole band mid grey and lets the two
 * things being measured announce themselves as the levels either side of it:
 *
 *     black   the glass cut -- nothing reaches the panel here
 *     grey    drawn, and nothing is on top of it
 *     white   drawn, and the carousel's scroll arc is painted over it
 *
 * Three levels a blue backlight cannot collapse, and no stripes to resolve, so
 * bloom stops mattering. Everything the app puts on top -- the datum, the column
 * ticks, the label -- is BLACK, because black is the one colour the arc cannot
 * be confused with and cannot wash out.
 *
 * The ticks are the ruler: black 1px columns at 5, 10, 15, 20, 25 and 30 from
 * each edge, full height. The arc hides the ones it covers, so the innermost
 * hidden tick names the arc's reach -- which is the number `kSafeLeftInset` has
 * been standing in for since it was guessed.
 *
 * ## The proof (PROBE_STYLE=proof)
 *
 * Four rounds of probing produced a claim:
 *
 *     a pixel is drawable iff it is inside the circle of radius w/2
 *     centred on the band, so the leftmost drawable column in row y is
 *
 *         xL(y) = ceil(w/2 - sqrt((w/2)^2 - dy^2) - 1.0),   dy = y + 0.5 - h/2
 *
 * measured three times, on both edges, agreeing to within a pixel every time.
 * This style stops measuring and states the claim in a form the panel can
 * refute: three regions, drawn one on top of the next, whose visible result is
 * a prediction rather than a reading.
 *
 * The -1.0 is the correction the first attempt earned, and the reason this style
 * is worth having. It shipped as -0.5, which asks whether a pixel's *centre*
 * falls inside the circle. The panel refuted it: the mid-grey ring, drawn one
 * pixel outside and predicted to vanish, was visible along part of the arc --
 * and only part, which is the tell. The two rules differ on 22 of the 60 rows,
 * so a uniform off-by-one would have looked nothing like this. A pixel lights
 * when the circle clips it at all, not when it swallows its centre.
 *
 *     GRAY       one pixel OUTSIDE xL       drawn first, expected to vanish
 *     WHITE      exactly AT xL              expected as an unbroken 1px ring
 *     GRAY_DARK  one pixel INSIDE xL        the field the ring sits on
 *
 * Each is painted as a filled staircase, so the later ones cover the earlier
 * ones everywhere except the one-pixel margin where they differ. If the model is
 * right the panel shows a dark grey field, a white ring around all of it, and no
 * mid grey anywhere. Three ways it can fail, each of them informative:
 *
 *     mid grey visible outside the ring   the glass reaches further than claimed
 *     the white ring broken somewhere     the glass stops short there
 *     white thicker than a pixel          the model has the wrong curvature
 *
 * Greys and not hues, deliberately. The ruler established that this backlight
 * drags every colour toward blue and that adjacent bright and dark hues bloom
 * into each other; three neutrals at 85, 170 and 255 shift together under that
 * bias, so only luminance carries the signal and the bias cancels.
 *
 * The outside ring cannot be tested everywhere, and that is geometry rather than
 * an oversight: across the middle rows xL is already 0, so one pixel further out
 * is off the band entirely and there is nothing to draw. Those rows clamp to the
 * boundary and are painted over by it. The test has force exactly at the corners,
 * which is where the arc bites and where the whole question lives.
 *
 * ## The staircase (PROBE_STYLE=stair)
 *
 * The box series answered its question and raised a better one. It established
 * that the clipping is *circular*: at inset 0 all four sides are drawn, but the
 * left and right bars survive only across the middle of the band, because a
 * 240-wide band centred on a 240-diameter disc has its own corners off the
 * glass. Measured against that model, inset 2 keeps 77% of its bar height where
 * the geometry predicts 78%.
 *
 * What the box series could not give is the *shape* of that boundary, because
 * one rectangle samples one column per side. The staircase samples nine: a
 * full-height bar at each of x = 0, 2, 4, 6, 8, 10, 12, 16, 20 and the mirror of
 * each on the right. Every bar is clipped to the chord at its own column, so the
 * bars come out as a literal staircase and the boundary curve can be read off in
 * one photograph -- and read by *counting bars*, which survives the glare and
 * the off-axis angle that made measuring a single bar's height unreliable.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Glance/GlanceControl.hpp"
#include "SDK/Kernel/Kernel.hpp"

#ifndef PROBE_INSET
#error "PROBE_INSET must be defined by the build -- see Tools/build_all.sh"
#endif

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service() = default;

    void run();

private:
    /// Ask the kernel for the glance area. Never declines: a panel too small to
    /// draw in is the measurement this app exists to take, so it draws what it
    /// can and records what it was given.
    bool glanceConfig();

    /// Build the rectangle and the two labels, once per viewing.
    void glanceCreate();

    /// Write `probe.txt` beside the .uapp: what the kernel offered, and the
    /// exact rectangle this build asked for. The photograph says whether it was
    /// drawn; this says what was requested, and the two together are the
    /// measurement.
    void note();

    /// Send the form if anything invalidated it. See SunGlance's Service::push()
    /// for why setValid() is after the send and not at the end of building.
    void push();

    /// The inset this build was compiled for, as a signed value so the
    /// arithmetic below cannot wrap when the area is smaller than the inset.
    /// Unused in staircase mode, where the columns below take its place.
    static constexpr int32_t kInset = PROBE_INSET;


#ifdef PROBE_STAIR
    /// Columns to stand a full-height bar in, counted from each edge.
    ///
    /// Two pixels apart at the tight end and not one, because one bar plus one
    /// gap is the finest pitch a photograph of this panel resolves -- the box
    /// series measured a 1px line as a five-pixel run, so a 1px pitch would
    /// merge into a smear. Coarser past 12, where the chord is already flat and
    /// the interesting part is over.
    static constexpr int32_t kCols[]   = { 0, 2, 4, 6, 8, 10, 12, 16, 20 };
    static constexpr size_t  kColCount = sizeof(kCols) / sizeof(kCols[0]);
#endif

#ifdef PROBE_RULER
    /// One colour per display pixel from the edge inward. Six hues that stay
    /// distinct at two bits a channel and through a camera: anything relying on
    /// a shade -- grey against white, brown against yellow -- would be a reading
    /// error waiting to happen, and the whole point of this style is that the
    /// reading cannot be argued with.
    static constexpr uint8_t kRuler[] = {
        GlanceColor_t::GLANCE_COLOR_RED,          // column 0
        GlanceColor_t::GLANCE_COLOR_GREEN,        // column 1
        GlanceColor_t::GLANCE_COLOR_BLUE,         // column 2
        GlanceColor_t::GLANCE_COLOR_YELLOW_DARK,  // column 3
        GlanceColor_t::GLANCE_COLOR_CYAN,         // column 4
        GlanceColor_t::GLANCE_COLOR_WHITE,        // column 5
    };
    static constexpr size_t kRulerCount = sizeof(kRuler) / sizeof(kRuler[0]);
#endif

#ifdef PROBE_ARC
    /// Black tick columns from each edge, in display pixels. Five apart, which
    /// is coarse enough to survive bloom on a black-on-grey edge and fine enough
    /// to bracket a number that is currently guessed at 18.
    static constexpr int32_t kTicks[]    = { 5, 10, 15, 20, 25, 30 };
    static constexpr size_t  kTickCount  = sizeof(kTicks) / sizeof(kTicks[0]);
#endif

    /// A rectangle and two lines of text. The floor every SDK glance example
    /// asks for, so a kernel that refuses this refuses everything.
    static constexpr uint32_t kControlsNeeded = 3;

    SDK::Kernel      &mKernel;
    SDK::Glance::Form mGlance;

    // The three control views are locals in glanceCreate() and not members.
    // Nothing on this screen ever changes -- the border and the labels are
    // written once and then only re-sent -- so there is nothing to hold a
    // handle for. `ControlRectangle` also declares only its two-argument
    // constructor and so has no default one, which makes a member of it
    // impossible without a placeholder that would exist purely to be
    // overwritten. The controls themselves live in the Form's vector either way.

    /// What the kernel said, kept for note() and for the on-screen label.
    uint32_t mMaxControls = 0;

    /// The rectangle actually asked for, after clamping. Kept so note() reports
    /// what was drawn rather than what was intended.
    int32_t mRectX = 0;
    int32_t mRectY = 0;
    int32_t mRectW = 0;
    int32_t mRectH = 0;
};

#endif // SERVICE_HPP
