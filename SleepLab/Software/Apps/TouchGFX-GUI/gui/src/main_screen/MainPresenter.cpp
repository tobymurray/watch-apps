#include <gui/main_screen/MainView.hpp>
#include <gui/main_screen/MainPresenter.hpp>

MainPresenter::MainPresenter(MainView &v)
    : view(v)
{
}

void MainPresenter::activate()
{
}

void MainPresenter::deactivate()
{
}

void MainPresenter::onReportChanged(const Model::Report &r)
{
    view.onReportChanged(r);
}

void MainPresenter::onHistoryChanged(const Model::History &h)
{
    view.onHistoryChanged(h);
}
