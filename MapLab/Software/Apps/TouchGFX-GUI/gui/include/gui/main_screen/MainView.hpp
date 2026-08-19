#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/containers/LabCanvasView.hpp>

#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief One screen, four modes: menu, running, cards, staircase.
 *
 * There is no TouchGFX Designer in this environment, so a second screen would
 * have to be hand-written into the generated tree and maintained there --
 * which is the trap `MapKit`'s TrackFaceMap and `MapManager`'s MainView both
 * avoided by hand-building their widgets beside the generated ones. This does
 * the same: the inherited stopwatch face is hidden, and a canvas widget plus
 * six text lines are added and shown or hidden per mode.
 *
 * The text lines stay visible over the card canvas in Cards mode, deliberately
 * -- one of the twelve cards is a bed for exactly that, since a vector
 * renderer that ever draws labels will be compositing TouchGFX glyphs over a
 * blitted map and the halo has to survive it.
 */
class MainView : public MainViewBase
{
public:
    MainView() = default;
    virtual ~MainView() {}

    virtual void setupScreen() override;
    virtual void tearDownScreen() override;
    virtual void handleTickEvent() override;

    /// The inherited stopwatch list is hidden and never populated.
    virtual void lapListUpdateItem(LapListItem &item, int16_t itemIndex) override
    {
        (void)item;
        (void)itemIndex;
    }

    void onStatusChanged(const Model::Status &status);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    static constexpr uint16_t kLineBufSize = 40;
    static constexpr int      kLines       = 6;
    static constexpr int16_t  kLineHeight  = 22;
    static constexpr int16_t  kFirstLineY  = 46;

    void refresh();
    void refreshMenu(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshRunning(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshCards(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshStair(char text[kLines][kLineBufSize], const Model::Status &s);

    LabCanvasView                     mCanvasView;
    touchgfx::TextAreaWithOneWildcard mLine[kLines];
    touchgfx::Unicode::UnicodeChar    mBuf[kLines][kLineBufSize];

    /// True between handing a blit bench to the canvas widget and collecting
    /// its result one tick later.
    bool mBlitRunning = false;
    int  mBlitIndex   = -1;
};

#endif // MAINVIEW_HPP
