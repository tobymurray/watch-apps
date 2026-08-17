/*********************************************************************************/
/*** Hand-authored in the shape TouchGFX Designer would generate. See below.    ***/
/*********************************************************************************/
/*
 * Every other file in generated/ really is Designer output. This one is not, and
 * saying so matters: there is no TouchGFX Designer in this environment, so the
 * usual "DO NOT MODIFY, regenerate instead" banner would be an instruction
 * nobody can follow.
 *
 * It is kept as a base class anyway rather than folded into MainView, because
 * the surrounding machinery (FrontendHeapBase's type lists,
 * FrontendApplicationBase's makeTransition call, the View<MainPresenter>
 * template) all expect the two-layer shape. Keeping it means this app can be
 * opened in the Designer later without unpicking anything.
 *
 * It holds only the background. MapManager inherited a Designer-built stopwatch
 * face here and had to hide eleven widgets it could not use -- their wildcard
 * buffers were hardcoded at 12 and 4 characters. Starting from an empty screen
 * costs nothing and avoids carrying widgets whose only purpose is to be
 * invisible.
 *
 * `FwDumpGUI.touchgfx` was trimmed to match: it declares the same single screen
 * with no components, rather than carrying over Chrono's stopwatch design the
 * way MapManager's does. So opening this app in the Designer would regenerate
 * this file as it already is, and would leave MainView's hand-written widgets
 * alone -- which is the property that makes keeping the two-layer shape worth
 * anything.
 */
#ifndef MAINVIEWBASE_HPP
#define MAINVIEWBASE_HPP

#include <gui/common/FrontendApplication.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <mvp/View.hpp>
#include <touchgfx/widgets/Box.hpp>

class MainViewBase : public touchgfx::View<MainPresenter>
{
public:
    MainViewBase();
    virtual ~MainViewBase() {}
    virtual void setupScreen();

protected:
    FrontendApplication& application()
    {
        return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    }

    /*
     * Member Declarations
     */
    touchgfx::Box __background;
};

#endif // MAINVIEWBASE_HPP
