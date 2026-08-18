#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <SDK/GUI/SensorStatusRow.hpp>
#include <MapKit/AttributionFace.hpp>

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setGpsFix(bool state);

    // External-HR link status -> the heart icon in the sensor-status row.
    void setAccessoryStatus(uint8_t state, const char* name);

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

protected:
    using Menu = App::MenuNav::Root;

    bool mGpsFix = false;
    SDK::Gui::SensorStatusRow mSensorRow;

    /// The licence notice the map data owes, put up once per app launch and
    /// dismissed by any key. Owned here rather than in the generated tree
    /// because there is no TouchGFX Designer in this environment.
    MapKit::AttributionFace mAttribution;
    bool                    mAttributionUp = false;
    uint32_t                mAttributionTicks = 0;
    /// Ticks left in which key events are swallowed after the notice comes
    /// down. See dismissAttribution().
    uint32_t                mAttributionGuard = 0;

    /// Takes the notice down, from either route: a key press or the
    /// five-second auto-advance. One place, so the two cannot drift.
    void dismissAttribution();

    MenuItemConfig mItems[Menu::ID_COUNT] {};
    MenuItemConfig mCenterItems[Menu::ID_COUNT] {};

    touchgfx::Callback<MainView, MainMenuItem&,       int16_t> mUpdateItemCb;
    touchgfx::Callback<MainView, MainMenuCenterItem&, int16_t> mUpdateCenterItemCb;
    touchgfx::Callback<MainView, int16_t>                      mpAnimationMiddleCb;

    void setupItems();
    void updateItem(MainMenuItem& item, int16_t index);
    void updateCenterItem(MainMenuCenterItem& item, int16_t index);
    void onAnimationMiddle(int16_t index);

    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;
    void onConfirm();
    void updateBackground(int16_t index);
};

#endif // MAINVIEW_HPP
