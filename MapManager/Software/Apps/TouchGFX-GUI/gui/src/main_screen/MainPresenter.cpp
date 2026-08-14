#include <gui/main_screen/MainView.hpp>
#include <gui/main_screen/MainPresenter.hpp>

MainPresenter::MainPresenter(MainView& v)
    : view(v)
{
}

void MainPresenter::activate()
{
}

void MainPresenter::deactivate()
{
}

void MainPresenter::onProgressChanged(const Model::Progress &progress)
{
    view.onProgressChanged(progress);
}

void MainPresenter::onRosterChanged(const Model::Roster &roster)
{
    view.onRosterChanged(roster);
}
