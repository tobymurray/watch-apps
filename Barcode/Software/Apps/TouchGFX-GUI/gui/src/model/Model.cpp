#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

#if defined(SIMULATOR)
    #include "touchgfx/canvas_widget_renderer/CanvasWidgetRenderer.hpp"
    #ifdef _WIN32
    #include "Windows.h"
    #endif
    #include <chrono>
    #include <ctime>
#endif

Model::Model()
    : modelListener(0)
    , mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
    , mSender(mKernel)
    , mState(Barcode::makeUnsetState(Barcode::Problem::NoFile))
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(this);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(this);

#if defined(SIMULATOR)
    LOG_INFO("Application is running through simulator! \n");

    std::string fileStoreDir = SDK::Simulator::KernelHolder::Get().getFsPath();
    LOG_INFO("Path to files created by app:\n"
        "       [%s]\n", fileStoreDir.c_str());

    LOG_INFO("\n"
        "---------------------------------------------------\n"
        "|   For Simulation Button use keybaord Keys.      |\n"
        "|       Keys Keybaord:                            |\n"
        "|       1   L1,                                   |\n"
        "|       2   L2,                                   |\n"
        "|       3   R1,                                   |\n"
        "|       4   R2                                    |\n"
        "|                  /---------\\                    |\n"
        "|                 /           \\                   |\n"
        "| BUTTON UP   L1 |             | R1 BUTTON SELECT |\n"
        "|                |     UNA     |                  |\n"
        "|                |    WATCH    |                  |\n"
        "| BUTTON DOWN L2 |             | R2 BUTTON BACK   |\n"
        "|                 \\           /                   |\n"
        "|                  \\---------/                    |\n"
        "---------------------------------------------------\n"
    );
#endif
}

FrontendApplication& Model::application()
{
    return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
}

void Model::tick()
{
    if (mInvalidate) {
        mInvalidate = false;
        application().invalidate();
    }
}

void Model::exitApp()
{
    LOG_INFO("Manually exiting the application\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit(); // No return for real app

    // !!! For TouchGFX Simulator !!!
    // This function only sets a flag.
    // The current TouchGFX loop will be completed, meaning that depending
    // on where this function was called, Model::tick(), Model::handleKeyEvent(),
    // as well as handleTickEvent() and handleKeyEvent() for the
    // current screen will be called.
}

// IGuiLifeCycleCallback
void Model::onStart()
{
    LOG_INFO("called\n");

    // The service publishes a snapshot when it is told the GUI is up, so this
    // is only a safety net for the case where that message beat our handler
    // being registered.
    mSender.requestState();
}

void Model::onResume()
{
    LOG_INFO("called\n");

    mInvalidate = true;
    mSender.requestState();
}

void Model::onStop()
{
    LOG_INFO("called\n");
}

void Model::onSuspend()
{
    LOG_INFO("called\n");
}

// ICustomMessageHandler
bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    if (msg->getType() != CustomMessage::BARCODE_STATE) {
        return false;
    }

    mState = static_cast<CustomMessage::BarcodeState *>(msg)->state;

    if (modelListener) {
        modelListener->onBarcodeChanged(mState);
    }
    return true;
}
