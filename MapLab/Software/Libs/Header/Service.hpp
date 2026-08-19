/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   MapLab's Service: present because the packer requires one, and
 *          otherwise asleep.
 ******************************************************************************
 *
 * `app_merging.py` makes the GUI ELF mandatory for every app type except
 * Glance, and `una_app_build_app()` expects a Service ELF beside it. MapLab
 * needs the GUI and has no use for the Service -- see `Commands.hpp` for why
 * every measurement belongs on the GUI thread -- so this is the smallest
 * correct Service: block on the kernel queue, and exit when the screen closes.
 *
 * It exits with the GUI rather than persisting like `MapManager`'s. That is
 * the ordinary `Utility` shape and the right one here: this app measures when
 * somebody is holding the watch, and a lab bench that kept a thread resident
 * after its screen closed would be spending the device's battery to do
 * nothing at all.
 ******************************************************************************
 */

#ifndef MAPLAB_SERVICE_HPP
#define MAPLAB_SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel) : mKernel(kernel) {}

    /// Returns when the app is stopped or the GUI closes.
    void run();

private:
    SDK::Kernel &mKernel;
};

#endif // MAPLAB_SERVICE_HPP
