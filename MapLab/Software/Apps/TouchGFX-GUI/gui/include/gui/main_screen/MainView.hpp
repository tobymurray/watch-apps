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
    // Card mode moves the two caption lines down onto the bottom arc, out of
    // the card's way. 178 and 200 are as low as a 240 px round panel goes
    // while still giving a 192 px and a 156 px chord to write in -- the
    // second line is narrower because the chord is.
    static constexpr int16_t  kCardLine4Y  = 178;
    static constexpr int16_t  kCardLine5Y  = 200;
    static constexpr int16_t  kCardLine5W  = 156;
    /// 4-way offset copies in `paper`, under the ink, so the text cards ask
    /// the question their caption claims to: whether a halo saves a glyph over
    /// a fill. Without these the view drew plain white and the halo was never
    /// on the panel at all.
    static constexpr int      kHaloCopies  = 4;

    void layoutLines(bool cardMode);

    void refresh();
    void refreshMenu(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshRunning(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshCards(char text[kLines][kLineBufSize], const Model::Status &s);
    void refreshStair(char text[kLines][kLineBufSize], const Model::Status &s);

    LabCanvasView                     mCanvasView;
    touchgfx::TextAreaWithOneWildcard mLine[kLines];
    touchgfx::Unicode::UnicodeChar    mBuf[kLines][kLineBufSize];
    /// Halo copies for the two caption lines only; visible in card mode.
    touchgfx::TextAreaWithOneWildcard mHalo[2][kHaloCopies];
    touchgfx::Unicode::UnicodeChar    mHaloBuf[2][kHaloCopies][kLineBufSize];
    bool                              mCardLayout = false;

    /// True between handing a blit bench to the canvas widget and collecting
    /// its result one tick later.
    bool mBlitRunning = false;
    int  mBlitIndex   = -1;
};

#endif // MAINVIEW_HPP
