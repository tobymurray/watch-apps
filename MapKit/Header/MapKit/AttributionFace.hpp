/**
 ******************************************************************************
 * @file    AttributionFace.hpp
 * @brief   The credit the map data is owed, shown once at app startup.
 *
 * ODbL § 4.3 requires a notice wherever a rendered map is publicly used, and
 * the OSMF Attribution Guidelines let that notice live on "a splash screen or
 * pop-up shown when a user starts the app" rather than as an overlay on the
 * map. This is that screen.
 *
 * **It is dismissed by a key press, not by a timer.** The guidelines list
 * three alternative collapse conditions and the first is "immediately with a
 * dismiss interaction" — the five-second floor governs only attribution that
 * fades on its own with nobody touching anything. So there is no countdown
 * here, and the wearer is never made to wait. (An earlier reading of the
 * guideline treated those five seconds as a required duration, which would
 * have meant compositing timed text over a live map on a 240×240 panel with
 * three usable dark codes, for no legal gain.)
 *
 * Once per app launch is enough: "If attribution is presented to the user upon
 * application startup, it does not need to be presented to the user every time
 * the user looks at or interacts with the application."
 *
 * Hand-written Container, same as TrackFaceMap and for the same reason: there
 * is no TouchGFX Designer in this environment, so a screen that lived in the
 * generated tree could not be maintained here.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT SHOWS, AND WHAT IT REFUSES TO
 * ---------------------------------------------------------------------------
 * The strings come from the packs installed on the watch, not from the pack a
 * GPS fix happens to select — see MapSession's constructor. That is what lets
 * this screen exist at startup at all.
 *
 * The wording is each pack's own `ATTR`. It is never substituted for or
 * abbreviated: packs rendered from different sources owe different credit, and
 * only the pack knows which.
 *
 * A watch with more distinct sources than fit on a round 240 px panel shows
 * the first `kVisibleStrings` and says how many more there are, rather than
 * ellipsising a credit into something that reads as complete when it is not.
 * The rest belong on the About screen, which is where the guidelines expect
 * the full licence information to live anyway.
 ******************************************************************************
 */

#ifndef MAPKIT_ATTRIBUTIONFACE_HPP
#define MAPKIT_ATTRIBUTIONFACE_HPP

#include <MapKit/MapSession.hpp>

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

namespace MapKit
{

class AttributionFace : public touchgfx::Container
{
public:
    /// Panel size. The display is 240×240 and this screen fills it.
    static constexpr int16_t kSize = 240;

    /// Distinct credits shown here. Nearly every watch has exactly one — all
    /// packs from one pipeline carry the same string — so this is about the
    /// unusual watch, not the usual one. Two is what fits legibly above the
    /// dismiss hint on a round panel.
    static constexpr size_t kVisibleStrings = 2;

    AttributionFace();

    /// Whether this screen is owed at all. False when nothing on the watch
    /// supplied a credit, in which case there is nothing to show and a blank
    /// splash would only be in the way.
    static bool isOwedBy(const MapSession& session) { return session.attributionCount() > 0; }

    /// Whether to put it up now: owed, and not already shown this launch.
    ///
    /// "This launch" is process lifetime, which is why the flag is static.
    /// The guidelines are explicit that startup attribution need not repeat —
    /// including when an app returns to the foreground from the background —
    /// and a screen destroyed and rebuilt on every menu transition would
    /// otherwise show it again on each pass through the main screen.
    static bool shouldShowAtStartup(const MapSession& session)
    {
        return !sShownThisLaunch && isOwedBy(session);
    }

    /// Record that the wearer has seen it. Called when the screen is put up,
    /// not when it is dismissed: the obligation is discharged by presenting
    /// the notice, and a wearer who dismisses it fast has still been shown it.
    static void markShown() { sShownThisLaunch = true; }

    /// Fill in from the session's collected attribution. Call once, in
    /// setupScreen(); the strings cannot change while the app runs, because a
    /// pack cannot arrive while the app runs.
    void setSources(const MapSession& session);

private:
    /// Room for the visible strings, their separators, and the overflow line.
    /// PackCatalog::kMaxAttrLen is a byte cap on UTF-8, so it is also an upper
    /// bound on code points.
    static constexpr uint16_t kBodyBufSize =
        static_cast<uint16_t>(PackCatalog::kMaxAttrLen * kVisibleStrings + 64);

    /// One TextArea per rendered line, because TouchGFX substitutes a
    /// wildcard *after* laying the text out: `WIDE_TEXT_WORDWRAP` measures the
    /// typed text — which is `"<>"`, two characters wide — and never re-wraps
    /// what the wildcard puts there, so a long credit is silently clipped at
    /// the box edge. Wrapping therefore has to happen here, in code, before
    /// the text is handed over.
    static constexpr size_t   kMaxLines     = 6;
    static constexpr uint16_t kLineBufSize  = 48;
    /// Characters per line at `T_TMP_REGULAR_14` in a 192 px box. Poppins is
    /// proportional, so this is a deliberately conservative count rather than
    /// a measured width — checked against the longest real credit in the
    /// simulator.
    static constexpr size_t   kCharsPerLine = 22;
    static constexpr int16_t  kLineHeight   = 20;

    touchgfx::Box                     mBackground;
    touchgfx::TextAreaWithOneWildcard mLines[kMaxLines];
    touchgfx::TextAreaWithOneWildcard mHint;

    /// Process-lifetime, i.e. per app launch. See shouldShowAtStartup().
    static bool sShownThisLaunch;

    touchgfx::Unicode::UnicodeChar mLineBuf[kMaxLines][kLineBufSize];
    touchgfx::Unicode::UnicodeChar mHintBuf[24];
};

} // namespace MapKit

#endif // MAPKIT_ATTRIBUTIONFACE_HPP
