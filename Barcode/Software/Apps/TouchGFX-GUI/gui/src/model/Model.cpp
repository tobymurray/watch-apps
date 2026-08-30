#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include <cstdio>
#include <cstdlib>

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
    , mState(Barcode::makeUnsetState(Barcode::Problem::NoConfig))
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

namespace
{
// Not input.json and not settings.json: the phone never writes here and
// never reads it either, so there is no schema to keep and no risk of
// squatting on a name the companion app is watching. Just this app,
// remembering one small thing about itself.
constexpr char kLastIndexPath[] = "/last_code.txt";
} // namespace

uint8_t Model::lastIndex() const
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLastIndexPath);
    if (!file || !file->open()) {
        return 0;
    }

    char buf[4] = {};
    size_t read = 0;
    const bool ok = file->read(buf, sizeof(buf) - 1, read);
    file->close();
    if (!ok || read == 0) {
        return 0;
    }
    buf[read] = '\0';

    const long value = std::strtol(buf, nullptr, 10);
    return (value >= 0 && value < static_cast<long>(Barcode::kMaxCodes))
               ? static_cast<uint8_t>(value)
               : 0;
}

void Model::rememberIndex(uint8_t index)
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLastIndexPath);
    if (!file || !file->open(true, true)) {
        return;
    }

    char buf[4];
    const int len = std::snprintf(buf, sizeof(buf), "%u", index);
    if (len > 0) {
        size_t written = 0;
        file->write(buf, static_cast<size_t>(len), written);
    }
    file->close();
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
