#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPresenter(MainView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~MainPresenter() {}

    void start() { model->startPwm(); }
    void stop() { model->stopPwm(); }
    void exit() { model->exitApp(); }

    const Model::Status& status() const { return model->status(); }

    /// Milliseconds into the current rung, extrapolated to now.
    uint32_t liveElapsedMs() const { return model->liveElapsedMs(); }

    // ModelListener implementation
    virtual void onStatusChanged(const Model::Status& status) override;

private:
    MainPresenter();

    MainView& view;
};

#endif // MAINPRESENTER_HPP
