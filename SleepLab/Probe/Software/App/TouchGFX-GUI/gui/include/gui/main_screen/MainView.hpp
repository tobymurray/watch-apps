#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The probe's only screen: is this actually going to record tonight?
 *
 * Six lines of text and nothing else. The probe is a diagnostic that runs for
 * eight hours unattended, and this screen exists for the thirty seconds before
 * that -- long enough to see that the sensors resolved, that rows are reaching
 * storage and that the last minute delivered samples. Everything else about
 * the night is in the CSV, where a host script can re-read it as the questions
 * change.
 *
 * The inherited widget tree (MainViewBase) was Designer-generated for a
 * stopwatch face and reached here through MapManager's fork of it. Its
 * wildcard buffers are hardcoded far too small for this content and there is
 * no Designer in this environment to resize them, so the inherited widgets are
 * hidden in setupScreen() and this class adds its own -- a supported pattern,
 * just not the Designer path. Editing Designer's packed string table by hand
 * to relabel them would risk the offsets of every string packed after it.
 */
class MainView : public MainViewBase
{
public:
    MainView() = default;
    virtual ~MainView() {}

    virtual void setupScreen() override;
    virtual void tearDownScreen() override;
    virtual void handleTickEvent() override {}

    /// The inherited stopwatch list is hidden and never populated.
    virtual void lapListUpdateItem(LapListItem &item, int16_t itemIndex) override
    {
        (void)item;
        (void)itemIndex;
    }

    /// A fresh snapshot arrived from the service.
    void onStatusChanged(const Model::Status &status);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// Widest line is the sensor block, which is deliberately terse for
    /// exactly this reason.
    static constexpr uint16_t kLineBufSize = 48;

    /// Six lines is what fits between the round panel's chords at this size
    /// without either end clipping.
    static constexpr int      kLines       = 6;
    static constexpr int16_t  kLineHeight  = 22;
    static constexpr int16_t  kFirstLineY  = 52;

    void refresh();

    /// Render the subscription bitmask as a fixed-width block of letters, one
    /// per sensor, upper case for resolved and lower for not.
    ///
    /// Case rather than presence, so the block is the same width and the same
    /// letters in the same places every time -- a missing sensor is then a
    /// change you can see at a glance instead of one you have to read.
    void formatSensors(char *out, size_t outSize, uint16_t subscribed) const;

    touchgfx::TextAreaWithOneWildcard mLine[kLines];
    touchgfx::Unicode::UnicodeChar    mBuf[kLines][kLineBufSize];
};

#endif // MAINVIEW_HPP
