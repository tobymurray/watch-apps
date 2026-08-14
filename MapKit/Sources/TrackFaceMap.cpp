#include <MapKit/TrackFaceMap.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include <cstdio>

namespace MapKit
{

TrackFaceMap::TrackFaceMap()
{
    setPosition(0, 0, kSize, kSize);

    mMap.setPosition(0, 0, kSize, kSize);
    add(mMap);

    // Bottom-centre one-liner. Sized to the round bezel's actual chord width
    // at its lowest row (240x240 panel, r=120: a box any wider than ~154 px
    // here runs past the visible glass). Centred text (not the "_L"
    // left-aligned variant) keeps the glyph run symmetric about the panel's
    // centre, and word-wrap-ellipsis is a hard backstop so an unexpectedly
    // long pack-error string is truncated instead of overflowing the box into
    // the bezel-hidden corners.
    mStatus.setPosition(48, 188, 144, 24);
    mStatus.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
    mStatus.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP_ELLIPSIS);
    mStatus.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    mStatusBuf[0] = 0;
    mStatus.setWildcard(mStatusBuf);
    add(mStatus);
}

void TrackFaceMap::setSources(const MapSession& session)
{
    mMap.setSources(&session.container(), &session.cache(), &session.trace());
}

void TrackFaceMap::update(const MapSession& session)
{
    const MapStatus status = session.status();

    // Tiles are drawn only when the pack is both structurally valid AND
    // CRC-trusted. MapTileView's own guard is Container::isOpen(), which is
    // true from the structural open onward -- i.e. before the CRC verdict --
    // so deciding it here is what actually withholds tiles while verifying.
    if (session.renderable()) {
        mMap.setViewportWithTiles(session.centerX(), session.centerY(),
                                  session.zoom(), session.fix());
    } else {
        mMap.setTraceOnlyViewport(session.centerX(), session.centerY(),
                                  session.zoom(), session.fix());
    }

    // Three failure states that must not read alike, and none of which is a
    // crash: no pack covers here, no verdict yet, and confirmed corrupt. The
    // activity keeps recording throughout, so every string says what the map
    // is doing rather than apologising.
    char text[kStatusBufSize];
    switch (status) {
        case MapStatus::NoFix:
            std::snprintf(text, sizeof(text), "acquiring GPS");
            break;
        case MapStatus::NoPack:
            std::snprintf(text, sizeof(text), "no map for here");
            break;
        case MapStatus::PackError: {
            // Short prefix so the actionable part -- the reason -- survives
            // the ellipsis that guards the bezel's narrow lower chord.
            const char* why = session.packErrorText();
            std::snprintf(text, sizeof(text), "map: %.16s", why != nullptr ? why : "unreadable");
        } break;
        case MapStatus::Corrupt:
            // Named explicitly rather than routed through the open-result
            // text: a corrupt pack opened *fine* structurally, so describing
            // its OpenResult would print "ok" for a broken map.
            std::snprintf(text, sizeof(text), "map pack corrupt");
            break;
        case MapStatus::Verifying:
            // Neutral, non-alarming: expected on the first run after a fresh
            // pack is deployed, and resolves on its own once Map Manager's
            // background CRC pass finishes. Not an error.
            std::snprintf(text, sizeof(text), "verifying map");
            break;
        case MapStatus::OffCoverage:
            std::snprintf(text, sizeof(text), "off map  z%u", session.zoom());
            break;
        case MapStatus::Live:
        default:
            std::snprintf(text, sizeof(text), "z%u", session.zoom());
            break;
    }

    touchgfx::Unicode::strncpy(mStatusBuf, text, kStatusBufSize - 1);
    mStatusBuf[kStatusBufSize - 1] = 0;
    mStatus.setWildcard(mStatusBuf);
    mStatus.invalidate();
}

} // namespace MapKit
