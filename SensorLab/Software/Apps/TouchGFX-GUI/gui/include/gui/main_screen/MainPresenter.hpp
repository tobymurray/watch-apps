#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPresenter(MainView &v);
    virtual ~MainPresenter() {}

    virtual void activate();
    virtual void deactivate();

    void exit() { model->exitApp(); }

    const Model::State &state() const { return model->state(); }

    void requestUpdate() { model->requestUpdate(); }
    void send(CustomMessage::Command command) { model->send(command); }

    virtual void onStateChanged(const Model::State &state) override;

private:
    MainPresenter();
    MainView &view;
};

#endif // MAINPRESENTER_HPP
