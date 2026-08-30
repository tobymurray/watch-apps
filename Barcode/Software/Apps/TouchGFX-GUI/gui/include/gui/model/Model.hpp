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

    /**
     * @brief Which code was on screen when the app last cycled, if any.
     *
     * Read from a small file this app writes itself, never one the phone
     * touches -- unlike input.json, there is nothing here for SDK::AppConfig
     * to own, so this goes through the kernel's raw filesystem interface
     * directly. @retval 0 if nothing was ever saved, or the file could not be
     * read; the caller still has to clamp against the current code count,
     * since fewer codes may exist now than when this was written.
     */
    uint8_t lastIndex() const;

    /**
     * @brief Remember which code is on screen, so the app reopens on it.
     *
     * Failure is silent and left unretried: this is a convenience, not a
     * value a wearer entered, so the worst a lost write costs is reopening on
     * the first code instead of the last one shown -- today's behaviour.
     */
    void rememberIndex(uint8_t index);

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
