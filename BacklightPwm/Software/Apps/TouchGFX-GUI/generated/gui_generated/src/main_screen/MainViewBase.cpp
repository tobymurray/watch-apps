/*********************************************************************************/
/*** Hand-authored in the shape TouchGFX Designer would generate. See the header. **/
/*********************************************************************************/
#include <gui_generated/main_screen/MainViewBase.hpp>

#include <touchgfx/Color.hpp>

MainViewBase::MainViewBase()
{
    // Black, and opaque. The panel is a reflective memory LCD, so black is what
    // costs least to hold -- and an explicitly-painted background means no frame
    // can catch whatever the previous screen left in the framebuffer.
    __background.setPosition(0, 0, 240, 240);
    __background.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    add(__background);
}

void MainViewBase::setupScreen()
{
}
