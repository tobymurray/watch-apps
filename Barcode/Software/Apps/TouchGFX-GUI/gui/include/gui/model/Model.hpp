#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"

#include "gui/common/GuiConfig.hpp"

#include "Commands.hpp"
#include "Barcode.hpp"

class FrontendApplication;
class ModelListener;

/**
 * @class Model
 * @brief GUI-side mirror of the id the service owns.
 *
 * The service is the single source of truth; the GUI only ever displays the
 * last snapshot it was sent.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    Model();

    void bind(ModelListener *listener)
    {
        modelListener = listener;
    }

    FrontendApplication &application();
    void tick();

    /**
     * @brief Exits the application.
     * This method notifies the kernel that the application is exiting and performs
     * any necessary cleanup before termination.
     */
    void exitApp();

    /**
     * @brief Last snapshot received from the service.
     */
    const Barcode::State &barcode() const { return mState; }

protected:
    ModelListener* modelListener;           ///< Pointer to model listener

    // Fields required for GUI <-> Service communication
    const SDK::Kernel& mKernel;             ///< Reference to kernel interface

    bool mInvalidate = false;               ///< Request to redraw current screen

    CustomMessage::Sender mSender;          ///< Outbound commands to the service
    Barcode::State         mState;          ///< Last snapshot from the service

    // IGuiLifeCycleCallback
    virtual void onStart()   override;
    virtual void onResume()  override;
    virtual void onStop()    override;
    virtual void onSuspend() override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase *msg) override;
};

#endif // MODEL_HPP
