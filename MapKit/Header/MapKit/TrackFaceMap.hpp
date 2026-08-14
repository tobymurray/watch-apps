/**
 ******************************************************************************
 * @file    TrackFaceMap.hpp
 * @brief   The live-map face of an activity's Track screen: basemap + trace +
 *          a one-line status.
 *
 * Hand-written Container -- no generated base, no TouchGFX Designer edit. Each
 * app's TrackView owns one as an extra member beside the Designer-generated
 * faces and toggles it with the same setVisible() machinery. That is
 * deliberate and worth preserving: there is no Designer in this environment,
 * so a face that lives in the generated tree could not be maintained here at
 * all.
 *
 * All map data lives in the app's Model (screens are destroyed on every
 * transition); this face only renders it and formats the status line.
 ******************************************************************************
 */

#ifndef MAPKIT_TRACKFACEMAP_HPP
#define MAPKIT_TRACKFACEMAP_HPP

#include <MapKit/MapSession.hpp>
#include <MapKit/MapTileView.hpp>

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

namespace MapKit
{

class TrackFaceMap : public touchgfx::Container
{
public:
    /// Panel size. The watch's display is 240x240 and this face fills it.
    static constexpr int16_t kSize = 240;

    TrackFaceMap();

    /// Point the widget at the Model's map data. Called from setupScreen(),
    /// once per screen construction.
    void setSources(const MapSession& session);

    /// Repaint from the session's current state. Cheap enough to call on
    /// every GPS sample.
    void update(const MapSession& session);

private:
    static constexpr uint16_t kStatusBufSize = 32;

    MapTileView                       mMap;
    touchgfx::TextAreaWithOneWildcard mStatus;
    touchgfx::Unicode::UnicodeChar    mStatusBuf[kStatusBufSize];
};

} // namespace MapKit

#endif // MAPKIT_TRACKFACEMAP_HPP
