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

    const Model::Report  &report()  const { return model->report(); }
    const Model::History &history() const { return model->history(); }

    virtual void onReportChanged(const Model::Report &r) override;
    virtual void onHistoryChanged(const Model::History &h) override;

private:
    MainPresenter();
    MainView &view;
};

#endif // MAINPRESENTER_HPP
