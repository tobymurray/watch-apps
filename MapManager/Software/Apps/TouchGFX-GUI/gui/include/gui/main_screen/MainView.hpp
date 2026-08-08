#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The single screen: pack name, percent complete, ETA, pack counts.
 *
 * The inherited widget tree (MainViewBase) was Designer-generated for a
 * stopwatch face -- its wildcard buffers are hardcoded far too small for
 * this content (12 and 4 UnicodeChars) and there's no Designer available in
 * this environment to resize them. Rather than fight that, the inherited
 * widgets are hidden in setupScreen() and this class adds its own two
 * TextAreaWithOneWildcard members directly (same hand-written-widget
 * pattern as AthensRun's TrackFaceMap, in the SDK this app's verification
 * logic was first built for) -- fully supported, just not the Designer path.
 */
class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    virtual void handleTickEvent() override {}

    virtual void lapListUpdateItem(LapListItem &item, int16_t itemIndex) override { (void)item; (void)itemIndex; }

    /**
     * @brief Take in a new snapshot from the service.
     */
    void onProgressChanged(const Model::Progress &progress);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    static constexpr uint16_t kLineBufSize = 40;

    void refresh(const Model::Progress &progress);

    touchgfx::TextAreaWithOneWildcard mLine1; ///< Pack name + percent, or idle/status text.
    touchgfx::TextAreaWithOneWildcard mLine2; ///< ETA and pack counts.
    touchgfx::Unicode::UnicodeChar    mLine1Buf[kLineBufSize];
    touchgfx::Unicode::UnicodeChar    mLine2Buf[kLineBufSize];

    Model::Progress mLastProgress{};
};

#endif // MAINVIEW_HPP
