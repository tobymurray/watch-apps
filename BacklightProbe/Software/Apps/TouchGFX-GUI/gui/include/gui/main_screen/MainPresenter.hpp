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

    void start() { model->startProbe(); }
    void exit() { model->exitApp(); }

    const Model::Status& status() const { return model->status(); }

    /// Milliseconds into the current step, extrapolated to now. The screen draws
    /// this during an OBSERVE step and it is what a video of the watch is read
    /// against, so it has to move faster than the service publishes.
    uint32_t liveStepElapsedMs() const { return model->liveStepElapsedMs(); }

    // ModelListener implementation
    virtual void onStatusChanged(const Model::Status& status) override;

private:
    MainPresenter();

    MainView& view;
};

#endif // MAINPRESENTER_HPP
