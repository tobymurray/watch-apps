/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode service: owns the id, the GUI only renders it.
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <memory>

#include "SDK/AppConfig/AppConfig.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "Barcode.hpp"

/**
 * @class Service
 * @brief Background half of the app.
 *
 * There is nothing to time or animate here, so unlike Stopwatch the service
 * does not need to outlive the GUI: it exits as soon as the GUI is gone.
 *
 * It is also the only holder of SDK::AppConfig. That class is documented as
 * one instance per app on one thread -- two of them would share one
 * `<configFile>.tmp` with no locking -- so the GUI never reads the
 * configuration itself; it renders the snapshot this thread sends it.
 */
class Service
{
public:
    Service(SDK::Kernel &kernel);

    virtual ~Service() = default;

    void run();

private:
    SDK::Kernel           &mKernel;
    CustomMessage::Sender  mSender;
    Barcode::State         mState;

    /// Held by pointer, and never built in the constructor: see load().
    std::unique_ptr<SDK::AppConfig> mConfig;

    void handleCommand(SDK::MessageBase *msg);

    /**
     * @brief (Re)read the values file by building a fresh SDK::AppConfig.
     *
     * SDK::AppConfig reads its file once, in its constructor, and exposes no
     * way to re-read; the SDK's own answer is that a change on the phone
     * applies the next time the app is opened. Replacing the object is how
     * this app keeps the behaviour it had before -- edit the file, come back
     * to the screen, see the new id -- using only the public API.
     *
     * The cost is a full open-read-parse where this app used to compare a
     * (size, mtime) stamp and usually do nothing. On a file of a few dozen
     * bytes, on a GUI resume, that is not worth a stat to avoid.
     */
    void load();

    /**
     * @brief What one declared slot turned out to hold.
     *
     * Five ways to fail rather than one, because adopt() has to choose which
     * Problem to report when nothing is drawable and the wearer needs a
     * different thing done about each. It replaces a re-read of the raw value
     * that adopt() used to do to tell an empty slot from a refused one.
     *
     * BadDigitCount and BadCharacters split what used to be one BadValue for a
     * digit-only format: see Barcode::Refusal in Symbology.hpp for why the two
     * failures need different words on screen.
     */
    enum class Adopted : uint8_t {
        Yes,           ///< @p out holds a drawable code; its name may be empty.
        Empty,         ///< Nothing stored, which is what an unused slot looks like.
        BadValue,      ///< An id the chosen format cannot carry, for the generic reason.
        BadFormat,     ///< A format this app does not draw.
        BadDigitCount, ///< A digit-only format's id was empty, odd, or over-length.
        BadCharacters, ///< A digit-only format's id held a character other than 0-9.
    };

    /// @brief Read one declared slot into @p out.
    Adopted adoptCode(size_t index, Barcode::Code &out) const;

    /// Turn whatever the configuration currently holds into a publishable state.
    void adopt();

    void publish();
};

#endif // SERVICE_HPP
