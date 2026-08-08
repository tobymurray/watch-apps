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

    void exit() { model->exitApp(); }

    const Model::Progress &progress() const { return model->progress(); }

    // ModelListener implementation
    virtual void onProgressChanged(const Model::Progress &progress) override;

private:
    MainPresenter();

    MainView& view;
};

#endif // MAINPRESENTER_HPP
