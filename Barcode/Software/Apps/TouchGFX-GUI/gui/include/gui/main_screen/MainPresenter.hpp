#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

#include "Barcode.hpp"

using namespace touchgfx;

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPresenter(MainView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~MainPresenter() {}

    void exit() {
        model->exitApp();
    }

    const Barcode::State &barcode() const { return model->barcode(); }

    uint8_t lastIndex() const { return model->lastIndex(); }
    void rememberIndex(uint8_t index) { model->rememberIndex(index); }

    // ModelListener implementation
    virtual void onBarcodeChanged(const Barcode::State &state) override;

private:
    MainPresenter();

    MainView& view;
};

#endif // MAINPRESENTER_HPP
